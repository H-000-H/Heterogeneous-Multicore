# SPI 子系统架构（STM32F407）

**仅 Master 模式**：从设备树 probe 到 LL_SPI 轮询/DMA 传输的分层说明。

---

## 1. 分层概览

```
设备树 (stm32f407-spi.dtsi + board *.dts)
        ↓ dtc-lite → board_probe.c
bus/spi/spi_bus.c          — Host probe、Client 注册、总线锁、CS 控制、选 poll/DMA
        ↓ spi_hal_*()
hal/spi/spi_hal_stm32.c    — LL_SPI 寄存器配置与传输
        ↓ SPI1/2/3 + DMA
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
| **DTS** | host 引脚、hw-instance、DMA phandle、子设备 CS/mode/frequency | 不含 C 逻辑 |
| **bus/spi** | 解析 DTS、`bus_controller` / `bus_client`、持锁、软件 CS | 不直接写 SPI 寄存器 |
| **hal/spi** | `LL_SPI` 波特率/mode、poll/DMA 传输 | 不管 VFS / 锁 / CS |
| **vfs/spi** | `file_operations` + `dev_lifecycle`、`SPI_CMD_TRANSFER` | 不解析 Flash 命令 |
| **drivers/flash** | W25Q64 JEDEC / 页编程 / 擦除 | 不碰引脚映射 |

**外设初始化**：SPI 时钟与 GPIO 由 **CubeMX `MX_SPIx_Init()`** 在板级 `pre_execution` 完成；mini_tree 只配置运行时 mode/分频与传输，不重复开 RCC。

---

## 2. 目录与文件

| 路径 | 文件 | 作用 |
|------|------|------|
| `board/dtsi/` | `stm32f407-spi.dtsi` | SPI1 host + W25Q64 子节点模板 |
| `board/dtsi/` | `stm32f407-dma.dtsi` | `dma_spi1_tx` / `dma_spi1_rx` phandle |
| `board/dt-bindings/spi/` | `spi-parameter.h` | 默认 host-id、频率、mode |
| `bus/spi/` | `spi_bus.c` / `spi_bus.h` | Master 总线框架 |
| `hal/spi/` | `spi_hal.h` | HAL API（Master only） |
| `hal/spi/` | `spi_hal_stm32.c` | STM32F4 LL_SPI 实现 |
| `bus/dma/` | `dma_core.c`, `dma_stm32.c` | DMA 通道 request |
| `vfs/spi/` | `spi_vfs.c` | VFS（`heterogeneous,w25q64-master`） |
| `vfs/spi/include/` | `spi_vfs.h` | ioctl 与 `spi_vfs_transfer()` |
| `drivers/flash/` | `w25q64_spi_drv.c` | Flash 协议驱动 |

---

## 3. 设备树契约

| compatible | 驱动 | 说明 |
|------------|------|------|
| `stm32,spi-master` | `bus/spi/spi_bus.c` | SPI Host（Master） |
| `heterogeneous,w25q64-master` | `vfs/spi/spi_vfs.c` | Flash 功能节点（VFS client） |

**Host 节点**（`&spi1`）：

| 属性 | 含义 |
|------|------|
| `host-id` | 逻辑 host 编号（默认 `SPI_DEFAULT_HOST_ID` = 1） |
| `hw-instance` | 硬件 SPI 外设号（1→SPI1，见 `DTS_HW_SPI1`） |
| `mosi-port` / `mosi-pin` 等 | 逻辑引脚 → `hal_pin_map_hw_gpio()` |
| `dma-tx` / `dma-rx` | 指向 `stm32f407-dma.dtsi` 中 DMA 节点 |
| `max-transfer-buffer` | 单次传输上限（默认 4096 B） |

**Client 节点**（`&w25q64_master`）：

| 属性 | 含义 |
|------|------|
| `cs-port` / `cs-pin` | 软件 CS 引脚 |
| `spi-mode` | CPOL/CPHA 0–3 |
| `spi-max-frequency` | 目标 SCK（Hz），HAL 映射为 LL 分频 |

---

## 4. 核心数据结构

### `spi_bus_host`

| 字段 | 含义 |
|------|------|
| `cfg` | host_id、hw_instance、GPIO 号、max_transfer_sz |
| `hal_host` | `struct spi_hal_host` |
| `dma_tx` / `dma_rx` | 可选；长度 ≥ 32 B 时走 DMA |
| `bus_mutex` | 总线互斥锁 |
| `ref_count` | Client open 计数 |

### `spi_bus_client`

| 字段 | 含义 |
|------|------|
| `host` | 所属 bus host |
| `cfg` | mode、clock_speed_hz、cs_pin（port\|pin 编码） |
| `hw_open` | 是否已 `spi_bus_open()` |

---

## 5. 传输 API

### Bus 层

```c
int spi_bus_transfer(struct device* dev,
                     const uint8_t* tx, uint8_t* rx,
                     size_t len, uint32_t timeout_ms);
```

流程：加锁 → `spi_hal_device_config_apply` → 拉低 CS → poll 或 DMA → 拉高 CS → 解锁。

### VFS 层

| API | 用途 |
|-----|------|
| `SPI_CMD_TRANSFER` | 全双工 |
| `spi_vfs_transfer()` | 便捷包装 |
| `read` / `write` | 半双工 |

### HAL 层

| 函数 | 说明 |
|------|------|
| `spi_hal_host_init` / `deinit` | 绑定 `SPI_TypeDef*` |
| `spi_hal_device_config_apply` | mode + 分频 |
| `spi_hal_transfer_poll` | 轮询全双工 |
| `spi_hal_transfer_dma` | DMA 全双工 |

---

## 6. 典型调用链：W25Q64 读 JEDEC ID

```
w25q64_spi_drv
  → spi_vfs_transfer(flash->spi_dev, tx, rx, len, tmo)
    → spi_vfs.c  SPI_CMD_TRANSFER
      → spi_bus_transfer()
        → spi_hal_transfer_poll() 或 transfer_dma()
          → LL_SPI 收发
```

`w25q64_spi_drv` 以 `device_get_parent()` 取得 SPI **Host** 设备；`heterogeneous,w25q64-master` 节点由 `spi_vfs.c` probe。

**Probe 顺序**：`stm32,spi-master`（Host）→ `heterogeneous,w25q64-master`（Client + VFS）。

---

## 7. 引脚与配置流

```
DTS: cs-port / cs-pin, mosi-port / mosi-pin …
  → hal_pin_probe()  → hal_pin_t { port, pin }
  → hal_pin_map_hw_gpio()  → STM32 引脚编号（HAL 层当前用于 CS；MOSI/MISO/SCLK 由 Cube 初始化）
  → spi_hal_stm32.c  → LL_SPI 模式与分频
```

逻辑端口 enum 与 `gpio-ctl.h`（`GPIOA` …）同名同值。

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

1. **仅 Master**：无 SPI Slave 栈；勿引用 `spi_slave_*` API。
2. **CS 软件控制**：每个 client 独立 CS 引脚，传输前后由 bus 层 toggle。
3. **传输长度**：超过 `max-transfer-buffer` 返回 `VFS_ERR_INVAL`。
4. **Cube 与 mini_tree 分工**：引脚复用/时钟在 Cube；mode/频率/数据在 mini_tree HAL 每次 transfer 前 apply。
5. **compatible 分工**：dtc-lite 对 `heterogeneous,w25q64-master` 匹配 **spi_vfs**；Flash 驱动通过 parent 调 `spi_vfs_transfer`。
