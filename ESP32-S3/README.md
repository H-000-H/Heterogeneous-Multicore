# ESP32-S3 — 虚拟网卡 + FFT 协处理器

基于 **ESP32-S3** 与 **ESP-IDF** 的固件工程，运行 `mini_tree` 设备树框架。本板在系统中的角色是两块功能外设的桥接节点：


| 功能          | 总线              | 说明                                        |
| ----------- | --------------- | ----------------------------------------- |
| **虚拟网卡**    | USB OTG         | **CDC-ECM** 模式，接 **i.MX6ULL** 主机，枚举为以太网设备 |
| **FFT 计算器** | SPI             | 与外部主控通信，接收时域数据并返回频域（FFT）结果                |
| **烧录 / 调试** | USB Serial/JTAG | 接 **电脑**，`idf.py flash monitor` 与串口日志     |


## USB 接口分工

ESP32-S3 有两路 USB，用途不同，开发时不要混用：


| 物理接口                | 模式                 | 连接对象     | 用途                        |
| ------------------- | ------------------ | -------- | ------------------------- |
| **USB OTG**         | Device，**CDC-ECM** | i.MX6ULL | 虚拟网卡数据通路                  |
| **USB Serial/JTAG** | 内置 JTAG + CDC 串口   | PC       | 固件烧录、OpenOCD/JTAG 调试、运行日志 |


虚拟网卡走 **CDC-ECM**（不是 RNDIS）。i.MX6ULL 侧加载 `cdc_ether` / `usbnet` 后即可出现 `usb0` 类网卡接口。

```
        PC                          i.MX6ULL
         │                              │
         │ USB Serial/JTAG              │ USB OTG (CDC-ECM)
         │ 烧录 / monitor               │ 虚拟网卡
         ▼                              ▼
    ┌────────────────────────────────────────┐
    │              ESP32-S3                    │
    │  ┌──────────────┐    ┌───────────────┐  │
    │  │ Serial/JTAG  │    │  USB OTG      │  │
    │  │ (调试用)      │    │  ECM 网卡栈   │  │
    │  └──────────────┘    └───────────────┘  │
    │                        │ SPI           │
    │                        ▼               │
    │                   FFT 服务 (hal_spi)    │
    └────────────────────────────────────────┘
                         │
                         ▼
                  对端 SPI 主控 / FFT 前端
```

## 软件结构

```
ESP32-S3/
├── main/                 # app_main 入口
├── components/
│   ├── app/              # 应用层任务（RTOS 启动、LED 等）
│   └── mini_tree/        # 板级框架：设备树、HAL、驱动、OSAL
│       ├── hal_if/       # HAL 抽象（USB、SPI、DMA …）
│       ├── board/        # DTS / 设备表 / probe
│       └── drivers/      # 外设驱动
└── build/                # 构建产物（含 dtc-lite 生成的设备表）
```

- **USB 虚拟网卡**：USB **OTG** 口、**CDC-ECM** Device 模式，经 `hal_usb` 与 ESP-IDF USB 栈对接 i.MX6ULL。
- **SPI FFT**：走 `hal_spi`（`esp32,spi` 总线控制器 + `heterogeneous,fft-spi-slave` 功能子设备），引脚与速率由 `board/dts/*.dts` 配置。
- **烧录调试**：使用板载 **USB Serial/JTAG** 接电脑，与 OTG 网卡通路无关。

设备树说明见 `[components/mini_tree/board/docs/devicetree.md](components/mini_tree/board/docs/devicetree.md)`。

## 构建

需要已安装并导出环境的 [ESP-IDF](https://docs.espressif.com/projects/esp-idf/)。

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <USB-Serial-JTAG端口> flash monitor
```

烧录与串口监视走 **USB Serial/JTAG** 口，不要占用 OTG 口（OTG 留给 i.MX6ULL 网卡）。

默认板级 DTS：`components/mini_tree/board/dts/esp32-s3-devkitc-1.dts`。

## 相关工程

同仓库下还有面向其他 MCU 的 `mini_tree` 移植：

- `../CH32V307/`
- `../STM32F407ZGT6/`
- `../IMX6ULL/`

本 ESP32-S3 工程专用于 **USB 网卡 + SPI FFT** 场景；其余平台不承担上述两条数据通路。

> > > > > > > cde205c (initial commit)

