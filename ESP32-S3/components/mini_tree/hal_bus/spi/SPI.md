# SPI 子系统架构（ESP32-S3）

从设备树 probe 到 ESP-IDF `spi_master` / `spi_slave` 硬件传输的分层说明。

---

## 1. 分层概览

```
设备树 (esp32s3-spi.dtsi + board *.dts)
        ↓ dtc-lite → board_probe.c
bus/spi/spi_bus.c          — Host probe、Client 注册、总线锁、路由 Master/Slave 传输
        ↓ spi_hal_*()
hal/spi/spi_hal_esp32.c    — ESP-IDF 执行层（poll / DMA / slave queue）
        ↓ spi_bus_initialize / spi_slave_initialize / spi_device_transmit …
硬件 (SPI2 / SPI3 + DMA)

────────────────── 上层 ──────────────────

功能驱动 (w25q64_spi_drv / fft_spi_drv)
        ↓ spi_vfs_transfer / read·write·ioctl
vfs/spi/spi_vfs.c          — Master 侧 VFS client（heterogeneous,w25q64-master）
vfs/spi/spi_slave_vfs.c    — Slave 侧 VFS client（heterogeneous,fft-spi-slave）
        ↓ spi_bus_transfer / spi_bus_slave_*()
bus/spi/spi_bus.c
```

**职责划分**

| 层 | 做什么 | 不做什么 |
|----|--------|----------|
| **DTS** | 声明 host 引脚、角色、子设备 CS/mode/frequency | 不含 C 逻辑 |
| **bus/spi** | 解析 DTS、注册 `bus_controller` / `bus_client`、持锁、选 poll/DMA | 不直接调 ESP-IDF |
| **hal/spi** | 封装 ESP-IDF SPI API、管理 per-host 私有状态 | 不管 VFS / 生命周期 |
| **vfs/spi** | `file_operations` + `dev_lifecycle`、ioctl 命令 | 不解析板级 flash 协议 |
| **drivers/** | W25Q64 命令序列、FFT 占位委托 | 不碰 GPIO 编号映射 |

---

## 2. 目录与文件

| 路径 | 文件 | 作用 |
|------|------|------|
| `board/dtsi/` | `esp32s3-spi.dtsi` | SoC SPI 节点模板、compatible 契约 |
| `board/dt-bindings/spi/` | `spi-parameter.h` | 默认 host-id、频率、mode、queue-size |
| `bus/spi/` | `spi_bus.c` / `spi_bus.h` | Host probe、Client 注册、传输入口 |
| `hal/spi/` | `spi_hal.h` | HAL 公共类型与 API |
| `hal/spi/` | `spi_hal_esp32.c` | ESP32-S3 平台实现 |
| `bus/dma/` | `dma_core.c`, `dma_esp32.c` | Master 大传输可选 DMA 通道 |
| `vfs/spi/` | `spi_vfs.c` | Master VFS（`heterogeneous,w25q64-master`） |
| `vfs/spi/` | `spi_slave_vfs.c` | Slave VFS（`heterogeneous,fft-spi-slave`） |
| `vfs/spi/include/` | `spi_vfs.h` | 公共 ioctl 与 `spi_vfs_transfer()` |
| `drivers/flash/` | `w25q64_spi_drv.c` | Flash 协议（JEDEC / 页编程 / 擦除） |
| `drivers/fft/` | `fft_spi_drv.c` | 委托 `spi_slave_vfs_probe()` |

---

## 3. 设备树契约

定义见 `board/dtsi/esp32s3-spi.dtsi`：

| compatible | 驱动 | 角色 |
|------------|------|------|
| `esp32,spi` | `bus/spi/spi_bus.c` | **Slave** 总线 Host |
| `esp32,spi-master` | `bus/spi/spi_bus.c` | **Master** 总线 Host |
| `heterogeneous,fft-spi-slave` | `vfs/spi/spi_slave_vfs.c` | Slave 功能节点（FFT） |
| `heterogeneous,w25q64-master` | `vfs/spi/spi_vfs.c` | Master 功能节点（Flash） |

**Host 节点属性**（`&spi1` / `&spi2`）：

| 属性 | 含义 |
|------|------|
| `host-id` | ESP-IDF `spi_host_device_t`（默认见 `DTS_SPI_*_HOST_ID`） |
| `mosi-port` / `mosi-pin` 等 | 逻辑引脚，经 `hal_pin_probe()` → `hal_pin_map_hw_gpio()` |
| `dma-chan` | ESP-IDF DMA 通道，`-1` 表示自动 |
| `max-trans-buffer` | 单次传输上限（字节） |

**Client 节点属性**（`&fft_slave` / `&w25q64_master`）：

| 属性 | 含义 |
|------|------|
| `cs-port` / `cs-pin` | CS 引脚 |
| `spi-mode` | CPOL/CPHA 模式 0–3 |
| `spi-max-frequency` | Master 时钟（Hz） |
| `queue-size` | Slave 异步队列深度 |

**DevKitC-1 默认**：`spi1`（Slave + FFT）= okay；`spi2`（Master + W25Q64）= disabled。

---

## 4. 核心数据结构

### 4.1 `spi_bus_host`（每 DTS host 节点一份）

| 字段 | 含义 |
|------|------|
| `cfg` | `host_id`、引脚 GPIO 号、`bus_role`、`max_transfer_sz`、DMA id |
| `hal_host` | 平台 HAL 宿主（`struct spi_hal_host`） |
| `dma_tx` / `dma_rx` | 可选 DMA 通道（Master，长度 ≥ 32 字节时走 DMA） |
| `bus_mutex` | 总线互斥锁 |
| `ref_count` | Client `open` 引用计数 |

### 4.2 `spi_bus_client`（每个 CS 子设备一份）

| 字段 | 含义 |
|------|------|
| `host` | 所属 `spi_bus_host` |
| `cfg` | mode、clock、cs_pin（port\|pin 编码）、queue_size |
| `hw_open` | 是否已 `spi_bus_open()` |

### 4.3 `spi_hal_host`（HAL 层，per hw_instance）

| 字段 | 含义 |
|------|------|
| `cfg` | 与 bus host 对应的硬件配置 |
| `hw_priv` | 指向 `spi_hal_esp32.c` 内 `spi_hal_esp32_priv` |

Slave 侧 HAL 还维护：`slave_trans`、`trans_queued`（原子标志）、静态 TX/RX 缓冲（默认 2048 B）。

---

## 5. 传输 API

### 5.1 Bus 层（`spi_bus.h`）

```c
/* Master：全双工；Slave 节点会自动转 spi_bus_slave_sync */
int spi_bus_transfer(struct device* dev,
                     const uint8_t* tx, uint8_t* rx,
                     size_t len, uint32_t timeout_ms);

/* Slave 同步 */
int spi_bus_slave_sync(struct device* dev,
                       const uint8_t* tx, uint8_t* rx,
                       size_t len, uint32_t timeout_ms);

/* Slave 异步：入队 TX，稍后 get_trans_result 取 RX */
int spi_bus_slave_queue_tx(struct device* dev,
                           const uint8_t* data, size_t len,
                           uint32_t timeout_ms);
int spi_bus_slave_get_trans_result(struct device* dev,
                                   uint8_t* rx_data, size_t rx_cap,
                                   size_t* trans_len, uint32_t timeout_ms);
```

Master `spi_bus_transfer` 内部流程：加锁 → `spi_hal_device_config_apply` → 软件拉低 CS → poll 或 DMA 传输 → 拉高 CS → 解锁。

### 5.2 VFS 层（`spi_vfs.h`）

| ioctl / API | 用途 |
|-------------|------|
| `SPI_CMD_TRANSFER` + `spi_transfer_arg` | Master 全双工 |
| `SPI_CMD_READ` | Slave 同步读 |
| `SPI_CMD_QUEUE_TX` | Slave 异步入队 |
| `SPI_CMD_GET_TRANS_RESULT` | Slave 异步取结果 |
| `spi_vfs_transfer(dev, tx, rx, len, tmo)` | 便捷包装（内部走 `SPI_CMD_TRANSFER`） |

`SPI_CMD_DEINIT` 仅在 `spi_vfs_drv.h` 内可见（应用层 include `spi_vfs.h` 时被 poison）。

### 5.3 HAL 层（`spi_hal.h`）

| 函数 | Master | Slave |
|------|--------|-------|
| `spi_hal_host_init` / `deinit` | ✓ | ✓ |
| `spi_hal_device_config_apply` | ✓ | — |
| `spi_hal_transfer_poll` / `transfer_dma` | ✓ | — |
| `spi_hal_slave_device_open` / `close` | — | ✓ |
| `spi_hal_slave_transfer` | — | ✓ 同步 |
| `spi_hal_slave_queue_tx` / `get_trans_result` | — | ✓ 异步 |

---

## 6. 典型调用链

### 6.1 FFT Slave 异步 TX（spi1 已 enable）

```
应用 / 上层
  → device_write(fft_dev, …)  或  ioctl(SPI_CMD_QUEUE_TX)
    → spi_slave_vfs.c
      → spi_bus_slave_queue_tx()
        → spi_hal_slave_queue_tx()   [spi_hal_esp32.c]
          → spi_slave_queue_trans()

  → ioctl(SPI_CMD_GET_TRANS_RESULT)
    → spi_bus_slave_get_trans_result()
      → spi_hal_slave_get_trans_result()
        → spi_slave_get_trans_result()
```

### 6.2 W25Q64 读 JEDEC ID（spi2 enable 时）

```
w25q64_spi_drv
  → spi_vfs_transfer(flash->spi_dev, tx, rx, len, tmo)
    → spi_vfs.c  SPI_CMD_TRANSFER
      → spi_bus_transfer()
        → spi_hal_transfer_poll() 或 transfer_dma()
          → spi_device_transmit()
```

`w25q64_spi_drv` 通过 `device_get_parent()` 取得 SPI **总线 Host** 设备作为 `spi_dev`；功能节点由 `spi_vfs.c` probe（`heterogeneous,w25q64-master`）。

### 6.3 启动 probe 顺序（dtc-lite 生成）

1. `esp32,spi` / `esp32,spi-master` → `spi_bus_host_probe_*`（创建 Host）
2. 子节点 `heterogeneous,*` → `spi_vfs_probe` / `spi_slave_vfs_probe`（注册 Client + VFS ops）
3. `board_driver_probe()` 按依赖表顺序调用上述函数

---

## 7. Master 与 Slave 差异

| 项目 | Master (`esp32,spi-master`) | Slave (`esp32,spi`) |
|------|----------------------------|---------------------|
| ESP-IDF 初始化 | `spi_bus_initialize` + `spi_bus_add_device` | `spi_slave_initialize`（CS/mode 绑在 interface） |
| 同步 API | `spi_bus_transfer` | `spi_bus_slave_sync` |
| 异步 API | 无 | `spi_bus_slave_queue_tx` + `get_trans_result` |
| CS 管理 | 软件 `hal_gpio_fast_set_level` + HAL | ESP-IDF slave interface 固定 CS |
| DMA | 可选（≥ 32 B 且已 request dma-tx/rx） | 走 ESP-IDF slave DMA 配置 |
| 每 Host 多 Client | 支持（每 CS 一个 client） | **不支持**第二套 CS/mode（HAL 拒绝 reconfigure） |
| 默认 max xfer | 4096 B | 2048 B |

---

## 8. 引脚与配置流

```
DTS: cs-port / cs-pin, mosi-port / mosi-pin …
  → hal_pin_probe()  → hal_pin_t { port, pin }
  → hal_pin_map_hw_gpio()  → ESP32 GPIO 编号
  → spi_hal_esp32.c  → ESP-IDF spi_bus_config_t / spi_slave_interface_config_t
```

逻辑端口 enum 与 `gpio-ctl.h` 同名同值；DTS 只产出逻辑整数，硬件 GPIO 在运行时映射。

---

## 9. 相关头文件

| 用途 | 头文件 |
|------|--------|
| 上层 ioctl / transfer | `vfs/spi/include/spi_vfs.h` |
| VFS 内部（含 DEINIT） | `vfs/spi/include/spi_vfs_drv.h` |
| Bus API | `bus/spi/spi_bus.h` |
| HAL API | `hal/spi/spi_hal.h` |
| DTS 默认常量 | `board/dt-bindings/spi/spi-parameter.h` |

---

## 10. 限制与注意点

1. **Slave 单设备**：同一 `host-id` 上 ESP32 slave 只能绑定一组 CS/mode；第二个 client 会在 `spi_hal_slave_device_open` 失败。
2. **传输长度**：超过 `max-trans-buffer`（Host 或 Client DTS）返回 `VFS_ERR_INVAL`。
3. **compatible 重复注册**：`w25q64_spi_drv.c` 与 `spi_vfs.c` 均声明 `heterogeneous,w25q64-master`；dtc-lite 匹配 **spi_vfs**。Flash 驱动通过 parent 设备调用 `spi_vfs_transfer`。
4. **board 默认**：DevKitC-1 仅启用 SPI Slave（FFT）；SPI Master + W25Q64 需在 `esp32-s3-devkitc-1.dts` 中打开 `&spi2` 与 `&w25q64_master`。
