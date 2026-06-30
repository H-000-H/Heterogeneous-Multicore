# STM32 SPI HAL 重构计划 — 按 GPIO 规则硬件直投

## 概述

将 GPIO 重构的硬件直投模式推广到 STM32 SPI 子系统。核心变更：
- 消灭 `hal_pin_t`（已被 GPIO 重构删除，当前 SPI 代码编译断裂）
- HAL 层去除 `s_spi_hosts[]` 静态池，`hal_spi_bus_host` 直接嵌入 bus 层
- DTSI 全部使用厂商宏值（`SPI1_BASE`、`LL_APB2_GRP1_PERIPH_SPI1`、`GPIOA_BASE`、`GPIO_PIN_7`、`GPIO_AF5_SPI1`、`LL_AHB1_GRP1_PERIPH_GPIOA`）
- HAL 层自行配置 MOSI/MISO/SCLK 为 AF 模式（不再依赖 CubeMX）
- 所有硬件操作用 LL 库函数，零查表零寄存器手写

## 当前状态分析

### 编译断裂点
GPIO 重构删除了 `hal_pin_t` 定义，以下文件引用 `hal_pin_t` 导致断裂：
- `hal/spi/hal_spi.h` — `hal_spi_bus_config.mosi/miso/sclk` 类型为 `hal_pin_t`
- `hal/spi/hal_spi_stm32.c` — `hal_pin_equal()` 调用
- `vfs/spi/spi_vfs.c` — `hal_pin_probe()`、`HAL_PIN_NUM/PORT` 宏
- `bus/spi/spi_bus.c` — `HAL_MAKE_PIN()` 解包

### 当前数据流（断裂前）
```
DTSI (DTS_GPIOA=0, DTS_GPIO_PIN_7=7)
  → VFS: hal_pin_probe → hal_pin_t{port=0, pin=7}
  → VFS: 打包 pin_num | (port << 16) → spi_bus_host_config.mosi_pin (int)
  → Bus: HAL_MAKE_PIN(port, pin) → hal_pin_t → hal_spi_bus_config.mosi
  → HAL: hal_pin_equal 比较 cs_pin
```
问题：逻辑索引 → 查表/打包/解包 → 逻辑索引，完全不是硬件直投。

### 目标数据流（GPIO 模式）
```
DTSI (GPIOA_BASE, GPIO_PIN_7, LL_AHB1_GRP1_PERIPH_GPIOA, GPIO_AF5_SPI1)
  → VFS: device_get_prop_int → int 临时变量 → 强转填入 hal_spi_bus_config
  → Bus: 直接透传 hal_spi_bus_config 给 HAL（零翻译）
  → HAL: LL_GPIO_SetPinMode / LL_GPIO_SetAFPin / LL_SPI_Init
```

## 变更清单

### 1. `hal/spi/hal_spi.h` — 完整重写

**新增引脚配置子结构体**（4 pin 共用，纯数据，非抽象层）：
```c
struct hal_spi_pin_cfg {
    GPIO_TypeDef* port;       /* GPIOA_BASE 强转 */
    uint16_t      pin_mask;   /* GPIO_PIN_x 掩码 */
    uint32_t      clk_periph; /* LL_AHB1_GRP1_PERIPH_GPIOx */
    uint32_t      af;         /* GPIO_AF5_SPI1 等 */
};
```

**`hal_spi_bus_config`** — 替换 `hal_pin_t` + `hw_instance`：
```c
struct hal_spi_bus_config {
    SPI_TypeDef*           spi_base;      /* SPI1_BASE 强转 */
    uint32_t               spi_clk_periph;/* LL_APB2_GRP1_PERIPH_SPI1 */
    struct hal_spi_pin_cfg mosi;
    struct hal_spi_pin_cfg miso;
    struct hal_spi_pin_cfg sclk;
    int                    max_transfer_sz;
    int                    dma_chan;
    int                    bus_role;
};
```

**`hal_spi_device_config`** — 替换 `hal_pin_t cs_pin`：
```c
struct hal_spi_device_config {
    int             mode;
    int             clock_speed_hz;
    GPIO_TypeDef*   cs_port;       /* CS 引脚端口基址 */
    uint16_t        cs_pin_mask;   /* CS 引脚掩码 */
    uint32_t        cs_clk_periph; /* CS 引脚时钟 */
    int             queue_size;
};
```

**`hal_spi_bus_host`** — 去池化，扁平字段替代 `hw_priv_storage`：
```c
struct hal_spi_bus_host {
    struct hal_spi_bus_config       cfg;
    struct hal_spi_device_config    active_cfg;
    SPI_TypeDef*                    SPIx;      /* 缓存 cfg.spi_base */
    struct osal_sem*                sync_sem;  /* DMA 同步信号量 */
    int                             hw_idx;    /* dummy buffer 索引 */
    int                             ref_count;
    bool                            bus_ready;
    bool                            hw_inited;
};
```
- 删除 `host_id` 字段（无池，对象由调用方提供）
- 删除 `HAL_SPI_HW_PRIV_SIZE` / `hw_priv_storage`（扁平字段替代）
- 删除 `struct hal_spi_stm32_priv`（字段合并入 `hal_spi_bus_host`）

**API 变更**：
```c
/* 旧: int hal_spi_bus_host_init(int host_id, const struct hal_spi_bus_config* cfg); */
int hal_spi_bus_host_init(struct hal_spi_bus_host* host, int hw_idx,
                          const struct hal_spi_bus_config* cfg);

/* 旧: int hal_spi_bus_host_deinit(int host_id); */
int hal_spi_bus_host_deinit(struct hal_spi_bus_host* host);

/* 删除: int hal_spi_bus_host_get(int host_id, struct hal_spi_bus_host** out); */
/* bus 层直接持有嵌入对象，无需 pool lookup */
```
- `hal_spi_dev_init` / `hal_spi_dev_hw_open` / `hal_spi_dev_hw_close` / `spi_sync` 签名不变
- `hal_spi_dev` 结构体不变（`ctlr` 指向 bus 层嵌入的 `hal_spi_bus_host`）

### 2. `hal/spi/hal_spi_stm32.c` — 重写

**删除**：
- `s_spi_hosts[HAL_SPI_HOST_MAX]` 静态池
- `stm32_spi_instance()` 函数（`spi_base` 直接由 config 提供）
- `stm32_priv()` 函数（字段已扁平化在 `hal_spi_bus_host` 中）
- `HAL_SPI_HOST_MAX` 改为仅用于 dummy buffer 尺寸

**`hal_spi_bus_host_init` 新实现**：
```c
int hal_spi_bus_host_init(struct hal_spi_bus_host* host, int hw_idx,
                          const struct hal_spi_bus_config* cfg)
{
    /* 1. SPI 外设时钟使能 */
    LL_APB2_GRP1_EnableClock(cfg->spi_clk_periph);

    /* 2. MOSI/MISO/SCLK 配置为 AF 模式 (LL 库, 零寄存器手写) */
    /*    对每个 pin: EnableClock + SetPinMode(ALTERNATE) + SetAFPin + SetPinOutputType + SetPinSpeed */
    hal_spi_config_af_pin(&cfg->mosi);
    hal_spi_config_af_pin(&cfg->miso);
    hal_spi_config_af_pin(&cfg->sclk);

    /* 3. 缓存 SPIx, 设置 hw_idx */
    host->SPIx = cfg->spi_base;
    host->hw_idx = hw_idx;
    host->cfg = *cfg;
    host->bus_ready = true;
    return VFS_OK;
}
```

新增 static helper（纯 LL 库调用，非抽象层）：
```c
static void hal_spi_config_af_pin(const struct hal_spi_pin_cfg* pin)
{
    LL_AHB1_GRP1_EnableClock(pin->clk_periph);
    LL_GPIO_SetPinMode(pin->port, pin->pin_mask, LL_GPIO_MODE_ALTERNATE);
    /* pin 0-7 用 SetAFPin_0_7, 8-15 用 SetAFPin_8_15 */
    if (pin->pin_mask < 0x100)
        LL_GPIO_SetAFPin_0_7(pin->port, pin->pin_mask, pin->af);
    else
        LL_GPIO_SetAFPin_8_15(pin->port, pin->pin_mask, pin->af);
    LL_GPIO_SetPinOutputType(pin->port, pin->pin_mask, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(pin->port, pin->pin_mask, LL_GPIO_SPEED_FREQ_HIGH);
}
```

**`hal_spi_bus_host_deinit` 新实现**：
```c
int hal_spi_bus_host_deinit(struct hal_spi_bus_host* host)
{
    /* 引用计数检查由 bus 层负责, HAL 只做硬件释放 */
    LL_SPI_Disable(host->SPIx);
    /* 引脚复位为 analog (同 GPIO deinit 模式) */
    hal_spi_reset_pin(&host->cfg.mosi);
    hal_spi_reset_pin(&host->cfg.miso);
    hal_spi_reset_pin(&host->cfg.sclk);
    host->bus_ready = false;
    return VFS_OK;
}
```

**`spi_sync` 修改**：
- `hal_pin_equal(dev->ctlr->active_cfg.cs_pin, dev->cfg.cs_pin)` → 
  `dev->ctlr->active_cfg.cs_port != dev->cfg.cs_port || dev->ctlr->active_cfg.cs_pin_mask != dev->cfg.cs_pin_mask`

**DMA 传输函数**：`hal_spi_transfer_dma_stm32` / `hal_spi_abort_stm32` 
- `stm32_priv(host)` → 直接用 `host`（字段已扁平化）
- `priv->SPIx` → `host->SPIx`
- `priv->sync_sem` → `host->sync_sem`
- `priv->hw_idx` → `host->hw_idx`

### 3. `bus/spi/spi_bus.h` — 修改

**包含 `hal_spi.h`**（获取 config 类型，同 `vfs-gpio.h` 包含 `hal_gpio.h` 模式）：
```c
#include "hal_spi.h"
```

**删除 `spi_bus_host_config` / `spi_bus_client_config`** — 直接用 HAL config 类型：
```c
/* 旧: struct spi_bus_host_config { int host_id; int hw_instance; int mosi_pin; ... }; */
/* 新: 直接用 hal_spi_bus_config (VFS 填充, bus 透传) */

/* 旧: struct spi_bus_client_config { int mode; int clock_speed_hz; int cs_pin; ... }; */
/* 新: 直接用 hal_spi_device_config */
```

**API 签名变更**：
```c
int spi_bus_host_init(struct device* dev, const struct hal_spi_bus_config* cfg);
int spi_bus_client_register(struct device* dev, const struct hal_spi_device_config* cfg,
                            struct spi_bus_client** out);
```

**Poison 列表调整**：
- 允许 config 类型: `hal_spi_bus_config`, `hal_spi_device_config`, `hal_spi_bus_host`, `hal_spi_dev`, `hal_spi_pin_cfg`
- 仍 poison HAL 函数: `hal_spi_bus_host_init`, `hal_spi_bus_host_deinit`, `hal_spi_dev_*`, `spi_sync` 等

### 4. `bus/spi/spi_bus.c` — 修改

**`spi_bus_host` 结构体** — 嵌入 HAL host（非指针）：
```c
struct spi_bus_host {
    struct device*               dev;
    struct hal_spi_bus_host      hal_host;  /* 嵌入, 非指针 */
    atomic_int                   ref_count;
    uint8_t                      in_use;
};
```

**`spi_host_init_impl`** — 去除 `HAL_MAKE_PIN` 翻译，直接透传：
```c
/* 旧: hal_cfg.mosi = HAL_MAKE_PIN((host_cfg->mosi_pin >> 16) & 0xFFFF, ...); */
/* 新: 直接传 host_cfg (已是 hal_spi_bus_config*) */
ret = hal_spi_bus_host_init(&host->hal_host, idx, host_cfg);
/* 无需 hal_spi_bus_host_get (对象已嵌入) */
```

**`spi_bus_open`** — CS 配置直接透传：
```c
/* 旧: dev_cfg.cs_pin = HAL_MAKE_PIN((client->cfg.cs_pin >> 16) & 0xFFFF, ...); */
/* 新: dev_cfg = client->cfg (已是 hal_spi_device_config) */
```

**`spi_client_register_impl`** — `client->cfg` 类型改为 `hal_spi_device_config`

### 5. `vfs/spi/spi_vfs.c` — 修改

**`spi_host_vfs` 结构体** — config 类型变更：
```c
struct spi_host_vfs {
    struct hal_spi_bus_config cfg;  /* 旧: spi_bus_host_config */
    int pool_idx;
};
```

**`spi_host_vfs_parse_dts`** — 删除 `hal_pin_probe`，改用 `device_get_prop_int` 直读：
```c
static int spi_host_vfs_parse_dts(struct device* dev, struct hal_spi_bus_config* cfg, int bus_role)
{
    int spi_base = 0, spi_clk = 0;
    int mosi_port = 0, mosi_pin = 0, mosi_clk = 0, mosi_af = 0;
    int miso_port = 0, miso_pin = 0, miso_clk = 0, miso_af = 0;
    int sclk_port = 0, sclk_pin = 0, sclk_clk = 0, sclk_af = 0;

    if (device_get_prop_int(dev, "spi-base", &spi_base) ||
        device_get_prop_int(dev, "spi-clk",  &spi_clk)  ||
        device_get_prop_int(dev, "mosi-port", &mosi_port) ||
        device_get_prop_int(dev, "mosi-pin",  &mosi_pin)  ||
        device_get_prop_int(dev, "mosi-clk",  &mosi_clk)  ||
        device_get_prop_int(dev, "mosi-af",   &mosi_af)   ||
        /* miso, sclk 同理 */)
        return VFS_ERR_INVAL;

    cfg->spi_base       = (SPI_TypeDef*)(uintptr_t)spi_base;
    cfg->spi_clk_periph = (uint32_t)spi_clk;
    cfg->mosi = (struct hal_spi_pin_cfg){
        .port = (GPIO_TypeDef*)(uintptr_t)mosi_port,
        .pin_mask = (uint16_t)mosi_pin,
        .clk_periph = (uint32_t)mosi_clk,
        .af = (uint32_t)mosi_af,
    };
    /* miso, sclk 同理 */
    cfg->bus_role = bus_role;
    /* dma_chan, max_transfer_sz 可选, 保留原逻辑 */
    return VFS_OK;
}
```

**`spi_vfs_parse_dts` (client)** — CS 引脚直读：
```c
static int spi_vfs_parse_dts(struct device* dev, struct hal_spi_device_config* cfg, int role)
{
    int cs_port = 0, cs_pin = 0, cs_clk = 0;

    if (device_get_prop_int(dev, "cs-port", &cs_port) ||
        device_get_prop_int(dev, "cs-pin",  &cs_pin)  ||
        device_get_prop_int(dev, "cs-clk",  &cs_clk)  ||
        device_get_prop_int(dev, "spi-mode", &cfg->mode) ||
        device_get_prop_int(dev, "spi-max-frequency", &cfg->clock_speed_hz))
        return VFS_ERR_INVAL;

    cfg->cs_port       = (GPIO_TypeDef*)(uintptr_t)cs_port;
    cfg->cs_pin_mask   = (uint16_t)cs_pin;
    cfg->cs_clk_periph = (uint32_t)cs_clk;
    cfg->queue_size = 0;
    /* slave 解析 queue-size, 保留原逻辑 */
    return VFS_OK;
}
```

**删除 `#include "hal_gpio.h"`**（不再需要 `hal_pin_probe`）

### 6. `vfs/spi/spi_vfs.h` — 无需修改
Poison 列表和便捷 API 不变。

### 7. `board/dtsi/stm32f407-spi.dtsi` — 重写

```dts
#include "stm32f407.dtsi"
#include "stm32f407-dma.dtsi"
#include <stm32f4xx.h>
#include <stm32f4xx_ll_bus.h>
#include <stm32f4xx_hal_gpio_ex.h>
#include <dt-bindings/spi/spi-parameter.h>

&soc {
    spi_cap: spi-cap {
        compatible = "stm32,spi-platform-cap";
        mini-tree,platform;
        host-max = <3>;
        max-xfer = <512>;
    };

    spi1: spi@0 {
        compatible = "stm32,spi-master";
        reg = <0>;
        #address-cells = <1>;
        #size-cells = <0>;
        spi-base = <SPI1_BASE>;
        spi-clk = <LL_APB2_GRP1_PERIPH_SPI1>;
        dma-chan = <SPI_DEFAULT_DMA_CHAN>;
        dma-tx = <&dma_spi1_tx>;
        dma-rx = <&dma_spi1_rx>;

        mosi-port = <GPIOA_BASE>;
        mosi-pin  = <GPIO_PIN_7>;
        mosi-clk  = <LL_AHB1_GRP1_PERIPH_GPIOA>;
        mosi-af   = <GPIO_AF5_SPI1>;

        miso-port = <GPIOA_BASE>;
        miso-pin  = <GPIO_PIN_6>;
        miso-clk  = <LL_AHB1_GRP1_PERIPH_GPIOA>;
        miso-af   = <GPIO_AF5_SPI1>;

        sclk-port = <GPIOA_BASE>;
        sclk-pin  = <GPIO_PIN_5>;
        sclk-clk  = <LL_AHB1_GRP1_PERIPH_GPIOA>;
        sclk-af   = <GPIO_AF5_SPI1>;

        status = "disabled";
        w25q64_master: w25q64@0 {
            compatible = "heterogeneous,w25q64-master";
            reg = <0>;
            spi-mode = <SPI_DEFAULT_MODE>;
            spi-max-frequency = <SPI_DEFAULT_MAX_FREQUENCY_HZ>;
            queue-size = <SPI_DEFAULT_QUEUE_SIZE>;
            cs-port = <GPIOA_BASE>;
            cs-pin  = <GPIO_PIN_4>;
            cs-clk  = <LL_AHB1_GRP1_PERIPH_GPIOA>;
            status = "disabled";
        };
    };
};
```

关键变更：
- `DTS_GPIOA` → `GPIOA_BASE`（厂商物理基址）
- `DTS_GPIO_PIN_7` → `GPIO_PIN_7`（厂商位掩码）
- `hw-instance` → `spi-base`（语义清晰）
- 新增 `spi-clk`、`mosi-clk/miso-clk/sclk-clk`、`mosi-af/miso-af/sclk-af`、`cs-clk`
- 删除 `host-id`（无池管理，不再需要）
- 新增 `<stm32f4xx_ll_bus.h>` 和 `<stm32f4xx_hal_gpio_ex.h>` include（提供 LL_APB2_GRP1_PERIPH_SPI1 和 GPIO_AF5_SPI1 宏）

## 不变部分

- DMA 传输接口 (`hal_spi_transfer_dma_stm32`, `hal_spi_abort_stm32`) — 签名不变，内部 `priv->` 改 `host->`
- `hal_spi_dev` 结构体 — 不变（`ctlr` 指针指向嵌入对象）
- `hal_spi_dev_init` / `hal_spi_dev_hw_open` / `hal_spi_dev_hw_close` — 签名不变
- VFS 层 lifecycle / fops / driver_register — 不变
- Bus 层 controller_ops / bus_ops / async API — 不变
- `stm32_spi_prescaler()` / `stm32_spi_transfer_poll()` / `hal_spi_wait_idle()` — 逻辑不变，`priv->SPIx` 改 `host->SPIx`

## 假设与决策

1. **SPI HAL 自行配置 GPIO 引脚**：MOSI/MISO/SCLK 在 `hal_spi_bus_host_init` 中配置为 AF 模式，不再依赖 CubeMX。与 GPIO HAL 自行配置引脚一致。CS 引脚配置仍由叶子驱动（w25q64）通过 GPIO 子系统管理，SPI HAL 仅存储 CS 信息用于配置变更检测。

2. **`hal_spi_pin_cfg` 子结构体**：4 个引脚各需 port/pin_mask/clk_periph/af 四字段，子结构体避免 16+ 扁平字段的冗长。这是数据组织，非抽象层包装。

3. **消除 HAL 池 + bus config 类型**：`hal_spi_bus_host` 嵌入 bus 层（非指针），`spi_bus_host_config`/`spi_bus_client_config` 合并入 HAL config 类型。VFS 直接填 `hal_spi_bus_config`，bus 层零翻译透传给 HAL。与 GPIO 的 `hal_gpio_obj_t` 嵌入 VFS 模式一致。

4. **`hw_idx` 由 bus 层传入**：dummy buffer 索引由 bus 层 pool index 提供，HAL 不做 SPI 基址→索引的查表。

5. **AF 选择函数 (pin 0-7 vs 8-15)**：用 `pin_mask < 0x100` 判断调用 `LL_GPIO_SetAFPin_0_7` 还是 `LL_GPIO_SetAFPin_8_15`。这是 LL 库 API 要求，非查表。

6. **`board_device.c` 的 `hal_pin_probe` 暂不处理**：它仍被 UART VFS 引用，会在 UART 迁移时修复。SPI 迁移后 SPI VFS 不再调用它。

## 验证步骤

1. **编译验证**：STM32 项目编译通过，无 `hal_pin_t` / `hal_pin_probe` / `HAL_MAKE_PIN` / `HAL_PIN_NUM` / `HAL_PIN_PORT` 残留引用（SPI 范围内）
2. **DTSI 预处理验证**：dtc-lite 正确展开 `GPIOA_BASE`、`LL_APB2_GRP1_PERIPH_SPI1`、`GPIO_AF5_SPI1` 等厂商宏
3. **符号隔离验证**：`spi_bus.h` poison 列表正确阻止外部调用 HAL 函数，允许 config 类型
4. **功能验证**：SPI probe → open → transfer → close 路径无回归（w25q64 驱动无需改动）
5. **CS 检测验证**：`spi_sync` 的 CS 变更检测 (`cs_port` + `cs_pin_mask` 比较) 等效于原 `hal_pin_equal`
