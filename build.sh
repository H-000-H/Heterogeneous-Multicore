#!/usr/bin/env bash
# ============================================================================
#  异构多核构建入口 — 三端默认 Docker (Docker / Linux 原生 / Windows 原生)
# ----------------------------------------------------------------------------
#  平台/工具链匹配由各 target 的 cmake/*.cmake 工具链文件 + CMakePresets.json
#  自动处理（find_program 三端探测：Docker /opt → Linux 标准路径 → Windows
#  标准路径 → PATH）。
#
#  本脚本仅决定“在哪个执行后端跑”：默认 Docker；Linux/Windows 可用 -native
#  强制宿主机原生；可用 -docker 强制覆盖。无需手动切换。
#
#  镜像映射（不写死路径，镜像内已三端探测工具链）:
#    ch32  → multi-arch-compiler:v2.0  (riscv + arm 工具链)
#    stm32 → multi-arch-compiler:v2.0  (arm 工具链)
#    esp32 → espressif/idf:latest       (官方 IDF 镜像，含 Xtensa + IDF)
#
#  用法:
#    ./build.sh ch32             # CH32V307  (RISC-V)   —— 默认 docker
#    ./build.sh stm32            # STM32F407 (ARM)      —— 默认 docker
#    ./build.sh esp32            # ESP32-S3  (Xtensa)   —— 默认 docker
#    ./build.sh all              # 一键构建所有节点
#    ./build.sh ch32 -native     # 强制原生
#    ./build.sh all -docker      # 全部强制 docker
# ============================================================================

set -e

# ── 镜像常量 ──
IMAGE_MULTI_ARCH="multi-arch-compiler:v2.0"
IMAGE_ESP_IDF="espressif/idf:latest"
BLDENV_DIR="$HOME/.local/share/bldenv"   # pip 装的 cmake/ninja 兜底位置（native 模式）

# ── 目标 → 目录 / preset 映射 ──
target_dir() {
    case "$1" in
        ch32)  echo "CH32V307"      ;;
        stm32) echo "STM32F407ZGT6" ;;
        esp32) echo "ESP32-S3"      ;;
        *)     return 1 ;;
    esac
}
target_preset() {
    case "$1" in
        ch32|stm32) echo "Debug" ;;
        esp32)      echo "esp-idf" ;;
        *)          return 1 ;;
    esac
}
# target → docker 镜像
target_image() {
    case "$1" in
        ch32|stm32) echo "$IMAGE_MULTI_ARCH" ;;
        esp32)      echo "$IMAGE_ESP_IDF"    ;;
        *)          return 1 ;;
    esac
}
# 三端统一默认 docker
target_default_backend() {
    echo "docker"
}

# ── OS 检测 ──
detect_os() {
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) echo "windows" ;;
        Linux*)               echo "linux"   ;;
        *)                    echo "linux"   ;;
    esac
}

# ── Windows 路径转 docker 挂载路径 ──
# Git Bash/MSYS 下 $(pwd) 形如 /d/path，docker desktop 需 D:\path 或 //d/path
host_mount_path() {
    local p="$1"
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*)
            # /d/path → D:\path （Windows docker 接受正斜杠）
            local drive="${p#/}"
            drive="${drive%%/*}"
            local rest="${p#/}"
            rest="${rest#*/}"
            echo "${drive^^}:/${rest}"
            ;;
        *)
            echo "$p"
            ;;
    esac
}

# ── cmake/ninja 发现：PATH 优先，否则用 BLDENV_DIR 兜底 ──
# $1=tail 时追加到 PATH 末尾（用于 ESP32 native，避免覆盖 IDF python_env）
ensure_cmake_ninja() {
    if command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
        return 0
    fi
    local mode="${1:-head}" bindir=""
    if [ -x "$BLDENV_DIR/bin/cmake" ] && [ -x "$BLDENV_DIR/bin/ninja" ]; then
        bindir="$BLDENV_DIR/bin"
    elif [ -x "/tmp/bldenv/bin/cmake" ] && [ -x "/tmp/bldenv/bin/ninja" ]; then
        bindir="/tmp/bldenv/bin"
    fi
    if [ -n "$bindir" ]; then
        if [ "$mode" = "tail" ]; then
            export PATH="$PATH:$bindir"
        else
            export PATH="$bindir:$PATH"
        fi
        return 0
    fi
    echo "错误: 未找到 cmake/ninja。请安装：pip install cmake ninja，或 apt install cmake ninja-build" >&2
    return 1
}

# ── docker 可用性 ──
docker_available() {
    command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1
}
docker_image_present() {
    docker image inspect "$1" >/dev/null 2>&1
}

# ── 原生构建 ──
build_native() {
    local t="$1" dir preset
    dir=$(target_dir "$t") || { echo "未知目标: $t"; exit 1; }
    preset=$(target_preset "$t")
    echo "==> [native] 构建 ${dir} (preset: ${preset}) ..."
    # ESP32 需激活 IDF 环境（IDF export 已自带 cmake/ninja/python_env，勿再覆盖 PATH）
    if [ "$t" = "esp32" ]; then
        if [ -z "$IDF_PATH" ] && [ -f "$HOME/esp-idf/export.sh" ]; then
            export IDF_PATH="$HOME/esp-idf"
        fi
        if [ -n "$IDF_PATH" ] && ! command -v idf.py >/dev/null 2>&1; then
            # shellcheck disable=SC1090
            . "$IDF_PATH/export.sh" >/dev/null 2>&1 || true
        fi
        if [ -z "$IDF_PATH" ]; then
            echo "错误: ESP32 需要 ESP-IDF，请先 source \$IDF_PATH/export.sh" >&2
            return 1
        fi
        ensure_cmake_ninja tail   # 追加到 PATH 末尾，保留 IDF python_env 优先
    else
        ensure_cmake_ninja
    fi
    cmake --preset "$preset" -S "$dir"
    cmake --build "$dir/build/$preset"
}

# ── docker 构建（三端统一，不写死路径）──
build_docker() {
    local t="$1" dir preset image root mnt
    dir=$(target_dir "$t") || { echo "未知目标: $t"; exit 1; }
    preset=$(target_preset "$t")
    image=$(target_image "$t")
    if ! docker_available; then
        echo "错误: docker 不可用，无法使用 -docker 后端" >&2
        return 1
    fi
    if ! docker_image_present "$image"; then
        echo "错误: 镜像 $image 不存在" >&2
        echo "   ch32/stm32 构建: docker build -t $IMAGE_MULTI_ARCH -f docker/Dockerfile.multi-arch ." >&2
        echo "   esp32 拉取: docker pull $IMAGE_ESP_IDF" >&2
        return 1
    fi
    echo "==> [docker] 构建 ${dir} (preset: ${preset}, image: ${image}) ..."
    root="$(cd "$(dirname "$0")" && pwd)"
    mnt="$(host_mount_path "$root")"

    # 构建命令：esp32 需先激活镜像内 IDF，其余直接 cmake --preset
    local cmd user_flag=""
    if [ "$t" = "esp32" ]; then
        cmd="source /opt/esp/idf/export.sh >/dev/null 2>&1 && cmake --preset '$preset' && cmake --build 'build/$preset'"
    else
        cmd="cmake --preset '$preset' && cmake --build 'build/$preset'"
        # ch32/stm32 以宿主用户运行，产物归属宿主（避免 root 属主导致宿主无法清理）
        user_flag="-u $(id -u):$(id -g)"
    fi

    docker run --rm \
        $user_flag \
        -v "$mnt:/workspace" \
        -w "/workspace/$dir" \
        "$image" \
        bash -c "$cmd"
}

# ── 单目标构建（按后端分发）──
build_one() {
    local t="$1" mode="$2" backend image
    if [ "$mode" = "auto" ]; then
        backend=$(target_default_backend "$t")
        # auto 下 docker 不可用则回退原生
        if [ "$backend" = "docker" ]; then
            image=$(target_image "$t")
            if ! docker_available || ! docker_image_present "$image"; then
                echo "   (docker 镜像 $image 不可用，回退 native)" >&2
                backend="native"
            fi
        fi
    else
        backend="$mode"
    fi
    if [ "$backend" = "docker" ]; then
        build_docker "$t"
    else
        build_native "$t"
    fi
}

usage() {
    cat <<EOF
用法: $0 <target> [-native|-docker]
  target:
    ch32    - CH32V307   (RISC-V)         默认 docker
    stm32   - STM32F407  (ARM Cortex-M4)  默认 docker
    esp32   - ESP32-S3   (Xtensa)         默认 docker
    all     - 一键构建所有节点
  选项:
    -native  强制宿主机原生构建
    -docker  强制 docker 容器构建

镜像:
    ch32/stm32 → $IMAGE_MULTI_ARCH
    esp32      → $IMAGE_ESP_IDF
构建镜像: docker build -t $IMAGE_MULTI_ARCH -f docker/Dockerfile.multi-arch .
工具链由 cmake 工具链文件三端自动探测，不写死路径。
EOF
    exit 1
}

# ── 主流程 ──
[ $# -ge 1 ] || usage

TARGET="$1"
MODE="auto"
case "${2:-}" in
    -native) MODE="native" ;;
    -docker) MODE="docker" ;;
    "")      MODE="auto"   ;;
    *)       echo "未知选项: $2"; usage ;;
esac

case "$TARGET" in
    ch32|stm32|esp32)
        build_one "$TARGET" "$MODE"
        ;;
    all)
        for t in ch32 stm32 esp32; do
            build_one "$t" "$MODE" || echo "!! ${t} 构建失败" >&2
        done
        ;;
    *)
        usage
        ;;
esac
