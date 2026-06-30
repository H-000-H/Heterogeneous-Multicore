# SPI 子系统架构（CH32V307）

**仅 Master 模式**：从设备树 probe 到 CH32 SPI 寄存器轮询/DMA 传输的分层说明。

---

## 1. 分层概览

```
设备树 (ch32v307-spi.dtsi + board *.dts)
        ↓ dtc-lite → board_probe.c
bus/spi/spi_bus.c          — Host probe、Client 注册、总线锁、CS 控制、选 poll/DMA
        ↓ spi_hal_*()
hal/spi/spi_hal_ch32.c     — SPI 寄存器配置与传输
        ↓ SPI1 + DMA
硬件

────────────────── 上层 ──────────────────

w25q64_spi_drv
        ↓ spi_vfs_transfer
vfs/spi/spi_vfs.c          — VFS client（heterogeneous,w25q64-master）
        ↓ spi_bus_transfer()
bus/spi/spi_bus.c
```

**职责划分**

| 层 | 做什么 | 不做什么 |
|----|--------|----------|
| **DTS** | host 引脚、host-id、子设备 CS/mode/frequency | 不含 C 逻辑 |
| **bus/spi** | 解析 DTS、`bus_controller` / `bus_client`、持锁、软件 CS | 不直接写 SPI 寄存器 |
| **hal/spi** | mode/分频、poll/DMA 传输 | 不管 VFS / 锁 / CS |
| **vfs/spi** | `file_operations` + `dev_lifecycle`、`SPI_CMD_TRANSFER` | 不解析 Flash 命令 |
| **drivers/flash** | W25Q64 JEDEC / 页编程 / 擦除 | 不碰引脚映射 |

**外设初始化**：SPI 时钟与 GPIO 由 **MounRiver / 板级 init** 完成；mini_tree 负责运行时配置与传输。

---

## 2. 目录与文件

| 路径 | 文件 | 作用 |
|------|------|------|
| `board/dtsi/` | `ch32v307-spi.dtsi` | SPI1 host + W25Q64 子节点模板 |
| `board/dt-bindings/spi/` | `spi-parameter.h` | 默认 host-id、频率、mode |
| `bus/spi/` | `spi_bus.c` / `spi_bus.h` | Master 总线框架 |
| `hal/spi/` | `spi_hal.h` | HAL API（Master only） |
| `hal/spi/` | `spi_hal_ch32.c` | CH32V307 实现 |
| `bus/dma/` | `dma_core.c`, `dma_ch32.c` | 可选 DMA |
| `vfs/spi/` | `spi_vfs.c` | VFS（`heterogeneous,w25q64-master`） |
| `vfs/spi/include/` | `spi_vfs.h` | ioctl 与 `spi_vfs_transfer()` |
| `drivers/flash/` | `w25q64_spi_drv.c` | Flash 协议驱动 |

---

## 3. 设备树契约

| compatible | 驱动 | 说明 |
|------------|------|------|
| `ch32,spi-master` | `bus/spi/spi_bus.c` | SPI Host（Master） |
| `ch32,spi-host` | `bus/spi/spi_bus.c` | 同上（兼容别名） |
| `heterogeneous,w25q64-master` | `vfs/spi/spi_vfs.c` | Flash 功能节点 |

**Host 节点**（`&spi1`）：

| 属性 | 含义 |
|------|------|
| `host-id` | 逻辑 host 编号（默认 `SPI_DEFAULT_HOST_ID` = 1） |
| `mosi-port` / `mosi-pin` 等 | DTSI 直投厂商宏值 (GPIOA_BASE / GPIO_Pin_7 / RCC_APB2Periph_GPIOA) |
| `dma-chan` | DMA 配置占位（-1 表示未指定） |
| `max-transfer-buffer` | 单次传输上限（默认 4096 B） |

**Client 节点**（`&w25q64_master`）：

| 属性 | 含义 |
|------|------|
| `cs-port` / `cs-pin` | 软件 CS |
| `spi-mode` | CPOL/CPHA 0–3 |
| `spi-max-frequency` | 目标 SCK（Hz） |

---

## 4. 核心数据结构

与 STM32 相同：`spi_bus_host`、`spi_bus_client`、`spi_hal_host`（见 `spi_bus.c` / `spi_hal.h`）。

- `hw_instance` 默认等于 `host-id`；CH32 当前映射 **SPI1**。
- 长度 ≥ 32 B 且已 request DMA 通道时走 `spi_hal_transfer_dma`。

---

## 5. 传输 API

### Bus 层

```c
int spi_bus_transfer(struct device* dev,
                     const uint8_t* tx, uint8_t* rx,
                     size_t len, uint32_t timeout_ms);
```

### VFS 层

| API | 用途 |
|-----|------|
| `SPI_CMD_TRANSFER` | 全双工 |
| `spi_vfs_transfer()` | 便捷包装 |

### HAL 层

| 函数 | 说明 |
|------|------|
| `spi_hal_host_init` / `deinit` | 绑定 `SPI_TypeDef*` |
| `spi_hal_device_config_apply` | mode + 分频 |
| `spi_hal_transfer_poll` | 轮询 |
| `spi_hal_transfer_dma` | DMA |

---

## 6. 典型调用链：W25Q64 读 JEDEC ID

```
w25q64_spi_drv
  → spi_vfs_transfer(flash->spi_dev, tx, rx, len, tmo)
    → spi_vfs.c  SPI_CMD_TRANSFER
      → spi_bus_transfer()
        → spi_hal_transfer_poll() 或 transfer_dma()
```

**Probe 顺序**：`ch32,spi-master`（Host）→ `heterogeneous,w25q64-master`（Client + VFS）。

---

## 7. 引脚与配置流

```
DTSI: cs-port / cs-pin, mosi-port / mosi-pin … (厂商宏值直投)
  → device_get_prop_int()  → hal_spi_config { port, pin, clk_periph, af, ... }
  → hal_spi_ch32.c  → SPI 模式与分频, GPIO AF 自配置
```

逻辑端口 enum 与 `gpio-ctl.h`（`GPIOA` … `GPIOE`）同名同值。

---

## 8. 相关头文件

| 用途 | 头文件 |
|------|--------|
| 上层 transfer | `vfs/spi/include/spi_vfs.h` |
| Bus API | `bus/spi/spi_bus.h` |
| HAL API | `hal/spi/spi_hal.h` |
| DTS 常量 | `board/dt-bindings/spi/spi-parameter.h` |

---

## 9. 限制与注意点

1. **仅 Master**：无 SPI Slave 实现。
2. **CS 软件控制**：bus 层在传输前后 toggle CS。
3. **传输长度**：默认上限 4096 B。
4. **compatible 分工**：dtc-lite 对 `heterogeneous,w25q64-master` 匹配 **spi_vfs**；Flash 驱动通过 parent 调 `spi_vfs_transfer`。
