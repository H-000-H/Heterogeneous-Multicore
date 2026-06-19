# mini_tree 设备树说明 (STM32F407)

编译期由 `tools/dtc-lite.py`（**PLY 词法分析** + 递归下降语法分析）解析 `board/dts/*.dts` 与 `board/dtsi/*.dtsi`，生成 `board_nodes.h`、`board_devtable.c`、`board_probe.c`、`dt_config_gen.h` 等。

## 文件布局（Linux 写法；dts / dtsi 分目录）

```
board/dts/stm32f407zgt6.dts        板级入口 (/dts-v1/, includes, / { }, &label)
board/dtsi/stm32f407.dtsi          SoC 根 / { compatible, cpus, soc: soc { ... } }
board/dtsi/stm32f407-spi.dtsi      IP: #include soc.dtsi + &soc { spi@0 { ... } }
board/dt-bindings/                 #include <dt-bindings/...> 常量
tools/dtc-lite.py                  CLI 入口（CMake 调用）
tools/dtc_lite/                    PLY 编译器实现
tools/vendor/ply/                  vendored PLY 3.x（构建无需 pip install）
```

| Linux 内核 | mini_tree (STM32) |
|------------|-------------------|
| `stm32f407.dtsi` | `board/dtsi/stm32f407.dtsi` |
| `stm32f407-dk.dts` | `board/dts/stm32f407zgt6.dts` |
| `&soc { ... };` | 同左 |
| `#include <dt-bindings/...>` | 同左（dtc-lite 从 `board/dt-bindings/` 解析） |

## dtc-lite 编译流水线

```
board/dts/*.dts → ① #include 预处理 → ② PLY lexer → ③ parser → AST
    → ④ compiler（overlay 合并 / 驱动校验）→ ⑤ generator → board_*.c/h
```

## dtc-lite 解析规则（无序全解耦版）

**无严格 include 顺序约束。** 多个 `/ { }` 任意合并；`&label { }` 延迟合并；未知 label 可自动创生（仍建议在 IP dtsi 写完整模板）。

旧约束「板级 `/ { }` 须紧接 SoC dtsi」「不可在两个根节之间插入 `&soc`」**已作废**。

## board *.dts 推荐布局

见 `board/dts/stm32f407zgt6.dts`：

1. `/dts-v1/`
2. `#include` SoC dtsi + IP dtsi（**可集中在文件头**）
3. `/ { model, compatible }`
4. `&label { ... }` — 启用外设时添加

## IP *.dtsi 推荐布局

见 `board/dtsi/stm32f407-spi.dtsi`：

1. `#include "stm32f407.dtsi"`
2. `#include <dt-bindings/...>`
3. `&soc { ... }` — 默认 `status = "disabled"`

板级 `.dts` 也会 include SoC dtsi；与 IP dtsi 内重复 include **由 dtc-lite 合并，不重复节点**。

## 职责分层

| 层级 | 文件 | 内容 |
|------|------|------|
| 常量 | `board/dt-bindings/*.h` | 仅 `#define` |
| SoC | `board/dtsi/stm32f407.dtsi` | `cpus` + `soc: soc { simple-bus }` |
| IP 模板 | `board/dtsi/stm32f407-spi.dtsi` 等 | `&soc { dev@reg }` |
| 板级 | `board/dts/stm32f407zgt6.dts` | includes + `/ { }` + `&label` |

## 节点路径惯例

外设路径形如 `/soc/<dev>@<reg>`。运行时通过 `device_get_prop_*()` 读取属性。

## 平台说明

SPI 模板见 `board/dtsi/stm32f407-spi.dtsi`（默认 disabled）。启用示例：

```dts
&spi1 {
	status = "okay";
	/* mosi-pin / miso-pin / sclk-pin 等待板级定义 */
};
&spi_dev0 {
	status = "okay";
	cs-pin = <N>;
};
```

须同时提供对应 `DRIVER_REGISTER` 驱动目录给 `dtc-lite`（CMake `mini_tree/CMakeLists.txt` 中配置）。

新增 IP 时参照 ESP32 仓库 `esp32s3-ws2812.dtsi` 与 ESP32 `board/docs/devicetree.md` binding 表。

---

## compatible: `stm32,spi-host` / `stm32,spi-device`

Bus Host + Interface 两级节点。模板见 `board/dtsi/stm32f407-spi.dtsi`；常量见 `board/dt-bindings/spi/spi-parameter.h`。
