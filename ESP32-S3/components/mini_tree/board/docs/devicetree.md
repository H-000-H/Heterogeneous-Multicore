# mini_tree 设备树说明

编译期由 `tools/dtc-lite.py` 解析 `board/dts/*.dts` 与 `board/dtsi/*.dtsi`，生成 `board_nodes.h`、`board_devtable.c`、`board_probe.c` 等。

## 文件布局（Linux 写法；dts / dtsi 分目录）

```
board/dts/esp32-s3-devkitc-1.dts   板级入口 (/dts-v1/, includes, / { }, &label)
board/dtsi/esp32s3.dtsi            SoC 根 / { compatible, cpus, soc: soc { ... } }
board/dtsi/esp32s3-ws2812.dtsi     IP: #include soc.dtsi + &soc { led@0 { ... } }
board/dtsi/esp32s3-spi.dtsi       IP: #include soc.dtsi + &soc { spi@0 { ... } }
board/dt-bindings/                 #include <dt-bindings/...> 常量
```

与 Linux 内核的对应（内容写法一致；目录可按项目拆分 dts / dtsi）：

| Linux 内核 | mini_tree |
|------------|-----------|
| `imx6q.dtsi` | `board/dtsi/esp32s3.dtsi` |
| `imx6q-sabresd.dts` | `board/dts/esp32-s3-devkitc-1.dts` |
| `&soc { uart@0 { ... }; }` | `&soc { led@0 { ... }; }` |
| `&uart0 { status = "okay"; };` | `&ws2812 { gpio = <48>; status = "okay"; };` |
| `#include <dt-bindings/...>` | 同左（dtc-lite 从 `board/dt-bindings/` 解析） |

板级 `.dts` 通过 `#include "esp32s3.dtsi"` 引用 dtsi（dtc-lite 在 `board/dtsi/` 中查找，等同 Linux 的 `-I` 搜索路径）。

## dtc-lite 解析规则（无序全解耦版）

**无严格 include 顺序约束。** 预处理 `#include` 展开后：

- 多个 `/ { }` 可任意顺序、任意位置，合并为同一根节点
- `&label { }` 延迟合并到目标节点（可写在板级 `.dts` 任意位置）
- 未知 label 可自动创生（宽松策略；仍建议在 IP dtsi 中写完整模板）

**不再适用**的旧说法：「板级 `/ { }` 必须紧接 SoC dtsi」「不能在两个根节之间插入 `&soc`」——这些是旧版 dtc-lite 的限制，已移除。

## board *.dts 推荐布局

见 `board/dts/esp32-s3-devkitc-1.dts`：

1. `/dts-v1/`
2. `#include` SoC dtsi + 需要的 IP dtsi（**可集中放在文件头**）
3. `/ { model, compatible, aliases }`
4. `&label { ... }` — gpio、引脚、`status = "okay"` 等

## IP *.dtsi 推荐布局

见 `board/dtsi/esp32s3-spi.dtsi`：

1. `#include "<soc>.dtsi"` — 便于 IDE（Devicetree LSP）单独打开时解析 `&soc`；板级 `.dts` 也会 include 同一 SoC dtsi，**dtc-lite 会合并，不会重复节点**
2. `#include <dt-bindings/...>` — 常量宏
3. `&soc { dev@reg { ... }; }` — 默认 `status = "disabled"`

## 职责分层

| 层级 | 文件 | 内容 |
|------|------|------|
| 常量 | `board/dt-bindings/*.h` | 仅 `#define`，由 IP dtsi `#include` |
| SoC | `board/dtsi/<soc>.dtsi` | `/ { compatible, cpus, soc: soc { simple-bus } }` |
| IP 模板 | `board/dtsi/<soc>-<ip>.dtsi` | `#include soc.dtsi` + `&soc { dev@reg }`、默认 disabled |
| 板级实例 | `board/dts/*.dts` | includes + `/ { }` + `&label` 覆写 |

`cpus { cpu@0 { ... status = "disabled"; }; }` 仅用于 IDE / 文档惯例，不参与 probe。

## IDE（Devicetree LSP）与编译（dtc-lite）

| | dtc-lite / build | Devicetree LSP |
|--|------------------|----------------|
| 权威来源 | **是** | 否（辅助编辑） |
| include 顺序 | 无要求 | 需正确 `devicetree.contexts` + includePaths |
| 自定义 compatible / 属性 | 支持 | 可能无 binding，跳转/校验有限 |
| 单开 `.dtsi` | 可编译（经板级 include 链） | 需 IP dtsi 内 `#include soc.dtsi` 或选中 context |

## 节点路径惯例

外设挂在 `simple-bus` 下，路径形如 `/soc/<dev>@<reg>`（例如 `/soc/led@0`）。

运行时通过 `device_get_prop_*()` 读取属性；属性在构建期展开为只读静态表（见 `device.h`）。

---

## compatible: `esp32,ws2812`

- **路径**: `/soc/led@N`
- **驱动**: `drivers/ws2812/ws2812_drv.c`
- **平台**: 仅 ESP32（STM32/CH32 无此节点）

### probe 必需属性

| 属性 | 类型 | 配置层级 | 说明 |
|------|------|----------|------|
| `reg` | u32 | IP dtsi | 总线地址 |
| `gpio` | int | board *.dts `&ws2812` | 数据引脚 |
| `num-leds` | int | board *.dts `&ws2812` | 灯珠数量，> 0 |
| `brightness` | int | IP dtsi | 0..255 |
| `bytes-per-led` | int | IP dtsi | 通常 3 |
| `color-order` | string | IP dtsi | `"grb"` / `"rgb"` 等 3 字母 |
| `default-timeout-ms` | int | IP dtsi | HAL 发送超时 (ms) |
| `rmt-resolution-hz` | int | IP dtsi / dt-bindings | RMT 时钟分辨率 |
| `rmt-mem-block` | int | IP dtsi / dt-bindings | RMT 内存块 |
| `rmt-queue-depth` | int | IP dtsi / dt-bindings | RMT 队列深度 |
| `t0h-ticks` | int | dt-bindings | 逻辑 0 高电平 tick |
| `t0l-ticks` | int | dt-bindings | 逻辑 0 低电平 tick |
| `t1h-ticks` | int | dt-bindings | 逻辑 1 高电平 tick |
| `t1l-ticks` | int | dt-bindings | 逻辑 1 低电平 tick |
| `reset-ticks` | int | dt-bindings | 复位低电平 tick |
| `status` | string | IP 默认 + board 覆盖 | `"disabled"` → board 改 `"okay"` |

时序常量见 `board/dt-bindings/led/ws2812-timing.h`；节点模板见 `board/dtsi/esp32s3-ws2812.dtsi`。

### 启用 / 禁用（仅 DTS，无 Kconfig 开关）

- **启用**：板级 include `esp32s3-ws2812.dtsi`，`&ws2812 { gpio = <N>; num-leds = <M>; status = "okay"; }`
- **临时禁用**：`&ws2812 { status = "disabled"; };`
- **完全移除**：去掉 `#include "esp32s3-ws2812.dtsi"`、`&ws2812 { }` 及 `aliases` 中的 `led0 = &ws2812`

`status = "okay"` 时 dtc-lite 须能扫描到 `DRIVER_REGISTER(ws2812, ...)`，否则构建失败。

### 驱动 API 侧约束

- `write(len==0)`: no-op
- `write(len>0)`: `len` 须为 `bytes-per-led` 整数倍且不超过 `num-leds` 帧长
- 详见 `drivers/ws2812/ws2812_drv.h`

---

## compatible: `esp32,spi` / `heterogeneous,fft-spi-slave`

SPI 采用 **Linux 风格总线 + 功能驱动** 模型：

| 节点 | compatible | 驱动 | probe 职责 |
|------|------------|------|------------|
| 父节点 `spi@N` | `esp32,spi` | `vfs/spi/spi_bus.c` | 总线引脚、DMA、枚举子设备 |
| 子节点 `fft@M` | `heterogeneous,fft-spi-slave` | `drivers/fft/fft_spi_drv.c` | 经 `spi_client` 绑定父总线；CS / 模式 / 队列 |

模板见 `board/dtsi/esp32s3-spi.dtsi`；板级在 `&spi1` 配置引脚，在 `&fft_slave` 配置 CS。

### 总线控制器属性 (`esp32,spi`)

| 属性 | 说明 |
|------|------|
| `#address-cells` / `#size-cells` | 子设备寻址（通常为 `<1>` / `<0>`） |
| `host-id` | SPI 控制器编号 |
| `mosi-pin` / `miso-pin` / `sclk-pin` | 总线引脚 |
| `dma-chan` | DMA 通道，`-1` 为自动 |
| `max-trans-buffer` | 最大传输字节 |

### SPI 客户端属性 (`heterogeneous,fft-spi-slave` 等功能 compatible)

| 属性 | 说明 |
|------|------|
| `reg` | 总线片选 / 地址 |
| `cs-pin` | 片选引脚 |
| `spi-mode` | 模式 0–3 |
| `spi-max-frequency` | 记录用（从机由主机定钟） |
| `queue-size` | 异步传输队列深度 |

### 生命周期

- **Bus 控制器**：`board_driver_probe_all()` 时 `esp32,spi` probe 初始化 HAL 并 `bus_client_bind` 子节点
- **SPI 客户端**：功能驱动 probe 调用 `spi_client_probe()`；`open` → attach；`close` → detach
- **I/O**：`hal_spi_xfer_begin` 持 bus 锁 + reconfigure，再 `host->bus.write/read`
