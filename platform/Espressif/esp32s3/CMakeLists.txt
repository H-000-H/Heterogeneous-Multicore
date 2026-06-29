cmake_minimum_required(VERSION 3.22)

set(ENV{IDF_PATH} "/home/ning/esp-idf")

# IDF 插件在运行 cmake 时不加载 export.sh,
# 手动补充交叉编译器和 Python 虚拟环境的路径.
# 同时补充 component manager 子进程所需的环境变量.
set(IDF_TOOLCHAIN_DIR "/home/ning/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin")
set(IDF_PYTHON "/home/ning/.espressif/tools/python/v6.0.1/venv/bin/python3")

# Component manager 通过子进程调用 idf.py, 需要以下环境变量:
set(ENV{IDF_PYTHON_ENV_PATH} "/home/ning/.espressif/tools/python/v6.0.1/venv")
set(ENV{ESP_IDF_VERSION} "6.0.1")
# 将工具链加入 PATH, 供 idf.py / esptool 等子进程使用
set(ENV{PATH} "${IDF_TOOLCHAIN_DIR}:$ENV{PATH}")

set(CMAKE_C_COMPILER   "${IDF_TOOLCHAIN_DIR}/xtensa-esp32s3-elf-gcc")
set(CMAKE_CXX_COMPILER "${IDF_TOOLCHAIN_DIR}/xtensa-esp32s3-elf-g++")
set(CMAKE_ASM_COMPILER "${IDF_TOOLCHAIN_DIR}/xtensa-esp32s3-elf-gcc")
list(PREPEND CMAKE_PROGRAM_PATH "${IDF_TOOLCHAIN_DIR}")
# ESP-IDF build.cmake 用 PYTHON 变量(而非 Python3_EXECUTABLE)调用 component manager
set(PYTHON "${IDF_PYTHON}")

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/components/components/app"
    "${CMAKE_SOURCE_DIR}/components/components/mini_tree")
    # managed_components 由 component manager 自动添加, 列出会导致组件重复

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3)
