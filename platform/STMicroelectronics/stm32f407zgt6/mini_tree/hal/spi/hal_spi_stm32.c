/* SPDX-License-Identifier: Apache-2.0 */
/*
 * SPI HAL — STM32F4 实现 (Master only)
 *
 * 设计: 硬件直投, DTSI 厂商宏值零翻译透传给 LL 库。
 * - hal_spi_bus_host 嵌入 bus 层, HAL 无 s_spi_hosts[] 池
 * - spi_sync: CS 变更检测用 cs_port + cs_pin 直接比较 (多设备共线不打架)
 * - slave / async 返回 VFS_ERR_NOTSUPP
 */
#include "hal_spi.h"
#include "dma.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_spi.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"

#include "dt_config_gen.h"

/* ── 平台参数：来自 DTS stm32,spi-platform-cap，无 DTS 时提供回退 ── */
#ifndef DTC_GEN_STM32_SPI_HOST_MAX
#define DTC_GEN_STM32_SPI_HOST_MAX  3
#endif
#ifndef DTC_GEN_STM32_SPI_MAX_XFER
#define DTC_GEN_STM32_SPI_MAX_XFER  512U
#endif

#define HAL_SPI_HOST_MAX   DTC_GEN_STM32_SPI_HOST_MAX
#define HAL_SPI_MAX_XFER   DTC_GEN_STM32_SPI_MAX_XFER

/* per-host dummy buffer: DMA 路径下 tx/rx 为 NULL 时填充占位, 防 cache line 踩踏。
 * - s_dummy_tx 填 0xFF: 用户只收时, DMA 仍需往 SPI->DR 写驱动 SCLK
 * - s_dummy_rx 丢弃区:  用户只发时, DMA 仍需从 SPI->DR 读避免 OVR
 * 32 字节对齐适配 DMA cache line; per-host 索引防多 host 并发踩踏。 */
static uint8_t s_dummy_tx[HAL_SPI_HOST_MAX][HAL_SPI_MAX_XFER] COMPAT_ALIGNED(32);
static uint8_t s_dummy_rx[HAL_SPI_HOST_MAX][HAL_SPI_MAX_XFER] COMPAT_ALIGNED(32);

/*============================================================================*/
/*                              LL 库直投 helper                              */
/*============================================================================*/
/* 纯 LL 库调用, 非抽象层 */
/**
 * @brief 配置 SPI 复用引脚: 时钟使能 + AF 模式 + 推挽高速 (LL 库直投)
 * @param pin 引脚配置 (含 port/pin/clk_periph/af)
 */
static void hal_spi_config_af_pin(const struct hal_spi_pin_cfg* pin)
{
    GPIO_TypeDef* port = (GPIO_TypeDef*)pin->port;
    LL_AHB1_GRP1_EnableClock(pin->clk_periph);
    LL_GPIO_SetPinMode(port, pin->pin, LL_GPIO_MODE_ALTERNATE);
    /* LL 库 API 按 pin 位域分两组: 0-7 走 AFRL, 8-15 走 AFRH */
    if (pin->pin < 0x100U)
        LL_GPIO_SetAFPin_0_7(port, pin->pin, pin->af);
    else
        LL_GPIO_SetAFPin_8_15(port, pin->pin, pin->af);
    LL_GPIO_SetPinOutputType(port, pin->pin, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(port, pin->pin, LL_GPIO_SPEED_FREQ_HIGH);
}

/**
 * @brief 复位 SPI 复用引脚为模拟模式 + 无上下拉 (等效去初始化)
 * @param pin 引脚配置
 */
static void hal_spi_reset_af_pin(const struct hal_spi_pin_cfg* pin)
{
    GPIO_TypeDef* port = (GPIO_TypeDef*)pin->port;
    LL_GPIO_SetPinMode(port, pin->pin, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinPull(port, pin->pin, LL_GPIO_PULL_NO);
}

/**
 * @brief 由目标时钟频率选最近档位的 LL 波特率预分频值
 * @param clock_hz 目标时钟频率 (Hz), <=0 时返回最大分频
 * @return LL_SPI_BAUDRATEPRESCALER_DIV* 宏值
 */
static uint32_t stm32_spi_prescaler(int clock_hz)
{
    if (clock_hz <= 0)
        return LL_SPI_BAUDRATEPRESCALER_DIV256;
    if (clock_hz >= 21000000) return LL_SPI_BAUDRATEPRESCALER_DIV2;
    if (clock_hz >= 10500000) return LL_SPI_BAUDRATEPRESCALER_DIV4;
    if (clock_hz >= 5250000)  return LL_SPI_BAUDRATEPRESCALER_DIV8;
    if (clock_hz >= 2625000)  return LL_SPI_BAUDRATEPRESCALER_DIV16;
    if (clock_hz >= 1312500)  return LL_SPI_BAUDRATEPRESCALER_DIV32;
    if (clock_hz >= 656250)   return LL_SPI_BAUDRATEPRESCALER_DIV64;
    if (clock_hz >= 328125)   return LL_SPI_BAUDRATEPRESCALER_DIV128;
    return LL_SPI_BAUDRATEPRESCALER_DIV256;
}

/**
 * @brief 轮询等待 SPI BSY 标志清零, 超时则直接返回
 * @param spi        SPI 外设寄存器基址
 * @param timeout_ms 超时 (ms)
 */
static void hal_spi_wait_idle(SPI_TypeDef* spi, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while (LL_SPI_IsActiveFlag_BSY(spi))
    {
        if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms)
            return;
    }
}

/*============================================================================*/
/*                              Host 管理 API                                 */
/*============================================================================*/
/**
 * @brief SPI Host 初始化: 使能 SPI 时钟 + 配置 MOSI/MISO/SCLK 复用引脚 + 缓存 fast path 字段
 * @param host  Host 对象指针 (由 bus 层嵌入)
 * @param hw_idx dummy buffer 索引 (per-host 防 DMA 踩踏)
 * @param cfg   总线配置 (DTSI 厂商宏值)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, spi 为空返回 VFS_ERR_NODEV
 */
int hal_spi_bus_host_init(struct hal_spi_bus_host* host, int hw_idx,
                          const struct hal_spi_bus_config* cfg)
{
    if (!host || !cfg || hw_idx < 0 || hw_idx >= HAL_SPI_HOST_MAX)
        return VFS_ERR_INVAL;
    if (host->bus_ready)
        return VFS_OK;
    if (!cfg->spi)
        return VFS_ERR_NODEV;

    __builtin_memset(host, 0, sizeof(*host));
    host->cfg = *cfg;

    if (host->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER &&
        host->cfg.bus_role != HAL_SPI_BUS_ROLE_SLAVE)
        host->cfg.bus_role = HAL_SPI_BUS_ROLE_MASTER;
    if (host->cfg.max_transfer_sz <= 0)
        host->cfg.max_transfer_sz = (int)HAL_SPI_MAX_TRANSFER_BYTES;
    else if (host->cfg.max_transfer_sz > (int)HAL_SPI_MAX_TRANSFER_BYTES)
        host->cfg.max_transfer_sz = (int)HAL_SPI_MAX_TRANSFER_BYTES;

    LL_APB2_GRP1_EnableClock(cfg->spi_clk_periph);

    hal_spi_config_af_pin(&cfg->mosi);
    hal_spi_config_af_pin(&cfg->miso);
    hal_spi_config_af_pin(&cfg->sclk);

    /* 缓存 fast path 字段 */
    host->spi      = cfg->spi;
    host->hw_idx   = hw_idx;
    host->bus_ready = true;
    return VFS_OK;
}

/**
 * @brief SPI Host 反初始化: 关闭 SPI + 复位 MOSI/MISO/SCLK 为模拟模式
 * @param host Host 对象指针
 * @return 成功返回 VFS_OK, host 为空返回 VFS_ERR_INVAL, 未就绪直接返回 VFS_OK
 */
int hal_spi_bus_host_deinit(struct hal_spi_bus_host* host)
{
    if (!host)
        return VFS_ERR_INVAL;
    if (!host->bus_ready)
        return VFS_OK;

    LL_SPI_Disable((SPI_TypeDef*)host->spi);
    /* 引脚复位为 analog (同 GPIO deinit 模式) */
    hal_spi_reset_af_pin(&host->cfg.mosi);
    hal_spi_reset_af_pin(&host->cfg.miso);
    hal_spi_reset_af_pin(&host->cfg.sclk);

    host->bus_ready = false;
    return VFS_OK;
}

/*============================================================================*/
/*                              Device 管理 API                               */
/*============================================================================*/
/**
 * @brief SPI Device 对象初始化: 清零 + 绑定 host + 拷贝 device 配置 (硬件尚未打开)
 * @param dev      Device 对象指针
 * @param pool_idx 设备在 bus 层池中的索引
 * @param host     所属 Host 对象指针
 * @param dev_cfg  设备配置 (mode/clock_speed/CS 等)
 */
void hal_spi_dev_init(struct hal_spi_dev* dev, int pool_idx,
                      struct hal_spi_bus_host* host,
                      const struct hal_spi_device_config* dev_cfg)
{
    if (!dev || !host || !dev_cfg || pool_idx < 0)
        return;

    __builtin_memset(dev, 0, sizeof(*dev));
    dev->pool_idx = pool_idx;
    dev->ctlr     = host;
    dev->cfg      = *dev_cfg;
}

/**
 * @brief 将 device 配置应用到 SPI 硬件: 关 SPI → 填 LL_SPI_InitTypeDef → 重启 SPI
 * @param host     Host 对象指针
 * @param dev_cfg  设备配置 (mode/clock_speed_hz)
 * @return 成功返回 VFS_OK, 参数非法或 LL_SPI_Init 失败返回 VFS_ERR_IO
 */
static int stm32_spi_apply_dev_cfg(struct hal_spi_bus_host* host,
                                   const struct hal_spi_device_config* dev_cfg)
{
    SPI_TypeDef*               spi;
    LL_SPI_InitTypeDef         init = {0};

    if (!host || !host->spi || !dev_cfg)
        return VFS_ERR_IO;

    spi = (SPI_TypeDef*)host->spi;

    LL_SPI_Disable(spi);
    init.TransferDirection = LL_SPI_FULL_DUPLEX;
    init.Mode              = LL_SPI_MODE_MASTER;
    init.DataWidth         = LL_SPI_DATAWIDTH_8BIT;
    init.ClockPolarity     = (dev_cfg->mode & 2) ? LL_SPI_POLARITY_HIGH : LL_SPI_POLARITY_LOW;
    init.ClockPhase        = (dev_cfg->mode & 1) ? LL_SPI_PHASE_2EDGE : LL_SPI_PHASE_1EDGE;
    init.NSS               = LL_SPI_NSS_SOFT;
    init.BaudRate          = stm32_spi_prescaler(dev_cfg->clock_speed_hz);
    init.BitOrder          = LL_SPI_MSB_FIRST;
    init.CRCCalculation    = LL_SPI_CRCCALCULATION_DISABLE;
    init.CRCPoly           = 7;

    if (LL_SPI_Init(spi, &init) != SUCCESS)
        return VFS_ERR_IO;

    LL_SPI_SetStandard(spi, LL_SPI_PROTOCOL_MOTOROLA);
    LL_SPI_Enable(spi);

    return VFS_OK;
}

/**
 * @brief 打开 SPI Device 硬件: 应用 device 配置 + 标记 hw_open + 增加 host 引用计数
 * @param dev Device 对象指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, host 未就绪返回 VFS_ERR_INVAL
 */
int hal_spi_dev_hw_open(struct hal_spi_dev* dev)
{
    struct hal_spi_bus_host* host;
    int                      ret;

    if (!dev || !dev->ctlr)
        return VFS_ERR_INVAL;

    if (dev->hw_open)
        return VFS_OK;

    host = dev->ctlr;
    if (!host->bus_ready)
        return VFS_ERR_INVAL;

    ret = stm32_spi_apply_dev_cfg(host, &dev->cfg);
    if (ret != VFS_OK)
        return ret;

    dev->hw_open = 1;
    host->ref_count++;
    host->active_cfg = dev->cfg;
    return VFS_OK;
}

/**
 * @brief 关闭 SPI Device 硬件: 减少 host 引用计数 + 标记 hw_open=0 (不关 SPI 外设)
 * @param dev Device 对象指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 未打开直接返回 VFS_OK
 */
int hal_spi_dev_hw_close(struct hal_spi_dev* dev)
{
    struct hal_spi_bus_host* host;

    if (!dev || !dev->ctlr)
        return VFS_ERR_INVAL;

    if (!dev->hw_open)
        return VFS_OK;

    host = dev->ctlr;
    if (host->ref_count > 0)
        host->ref_count--;

    dev->hw_open = 0;
    return VFS_OK;
}

/*============================================================================*/
/*                              同步传输 (Master)                             */
/*============================================================================*/
/**
 * @brief SPI 轮询传输: 逐字节 TXE/RXNE 标志轮询, 超时检测基于 HAL_GetTick
 * @param host        Host 对象指针
 * @param tx          发送缓冲区 (可为 NULL, 内部填 0xFF)
 * @param rx          接收缓冲区 (可为 NULL, 仅丢弃)
 * @param len         传输字节数
 * @param timeout_ms  超时 (ms)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 超时返回 VFS_ERR_TIMEOUT, 外设异常返回 VFS_ERR_IO
 */
static int stm32_spi_transfer_poll(struct hal_spi_bus_host* host,
                                    const uint8_t* tx, uint8_t* rx,
                                    size_t len, uint32_t timeout_ms)
{
    SPI_TypeDef*               spi;
    uint32_t                   start;
    size_t                     i;

    if (!host || len == 0)
        return VFS_ERR_INVAL;

    if (!host->spi)
        return VFS_ERR_IO;

    if (len > HAL_SPI_MAX_XFER)
        return VFS_ERR_INVAL;

    spi   = (SPI_TypeDef*)host->spi;
    start = HAL_GetTick();

    for (i = 0; i < len; i++)
    {
        uint8_t out = tx ? tx[i] : 0xFFU;

        while (!LL_SPI_IsActiveFlag_TXE(spi))
        {
            if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms)
                return VFS_ERR_TIMEOUT;
        }
        LL_SPI_TransmitData8(spi, out);

        while (!LL_SPI_IsActiveFlag_RXNE(spi))
        {
            if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms)
                return VFS_ERR_TIMEOUT;
        }
        if (rx)
            rx[i] = LL_SPI_ReceiveData8(spi);
        else
            (void)LL_SPI_ReceiveData8(spi);
    }

    hal_spi_wait_idle(spi, timeout_ms);
    return VFS_OK;
}

/**
 * @brief SPI 同步传输 (Master): 配置变更检测后转 stm32_spi_transfer_poll 执行
 * @param dev        Device 对象指针 (必须已 hw_open 且 host 为 master)
 * @param tx         发送缓冲区 (可为 NULL)
 * @param rx         接收缓冲区 (可为 NULL)
 * @param len        传输字节数
 * @param timeout_ms 超时 (ms)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 配置应用失败或超时返回对应 VFS_ERR_*
 */
int spi_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
             size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->ctlr || !dev->hw_open || len == 0)
        return VFS_ERR_INVAL;

    if (dev->ctlr->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    if (len > (size_t)dev->ctlr->cfg.max_transfer_sz)
        return VFS_ERR_INVAL;

    /* 配置变更检测: CS 引脚 + mode + clock 任一变化则重配 SPI。
     * 多设备共线时, 不同 device 的 cs_port/cs_pin 不同,
     * 切换设备自动触发重配, 保证不会读到上一个设备的残留配置。 */
    if (dev->ctlr->active_cfg.cs_port    != dev->cfg.cs_port    ||
        dev->ctlr->active_cfg.cs_pin     != dev->cfg.cs_pin      ||
        dev->ctlr->active_cfg.mode        != dev->cfg.mode        ||
        dev->ctlr->active_cfg.clock_speed_hz != dev->cfg.clock_speed_hz)
    {
        int ret = stm32_spi_apply_dev_cfg(dev->ctlr, &dev->cfg);
        if (ret != VFS_OK)
            return ret;
        dev->ctlr->active_cfg = dev->cfg;
    }

    return stm32_spi_transfer_poll(dev->ctlr, tx, rx, len, timeout_ms);
}

/*============================================================================*/
/*                              DMA 传输 (保留接口, 供 bus 层选用)             */
/*============================================================================*/
/* 注: 当前 spi_sync 走轮询路径 (stm32_spi_transfer_poll), DMA 路径需 bus 层
 * 解析 dma-tx/dma-rx phandle 拿 bus_dma_chan* 后调用本函数。dummy buffer
 * 服务于本路径: tx/rx 为 NULL 时填充占位, 防 DMA cache line 踩踏。 */
/**
 * @brief SPI DMA 完成 ISR 回调: 释放同步信号量 (ISR 上下文, 严禁阻塞)
 * @param chan      DMA 通道 (未使用)
 * @param user_data host 指针 (struct hal_spi_bus_host*)
 */
static void hal_spi_dma_isr(struct bus_dma_chan* chan, void* user_data)
{
    struct hal_spi_bus_host* host = (struct hal_spi_bus_host*)user_data;
    (void)chan;
    if (host && host->sync_sem)
        COMPAT_IGNORE_RESULT(osal_sem_post_from_isr(host->sync_sem, NULL));
}

/**
 * @brief SPI DMA 同步传输: 提交 RX/TX DMA + 等信号量 + 清理
 * @param host       Host 对象指针
 * @param dma_tx     TX DMA 通道 (bus 层提供)
 * @param dma_rx     RX DMA 通道 (bus 层提供)
 * @param tx         发送缓冲 (NULL 时用 s_dummy_tx 填 0xFF)
 * @param rx         接收缓冲 (NULL 时用 s_dummy_rx 丢弃)
 * @param len        字节数
 * @param timeout_ms 超时 (ms)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int hal_spi_transfer_dma_stm32(struct hal_spi_bus_host* host,
                               struct bus_dma_chan* dma_tx,
                               struct bus_dma_chan* dma_rx,
                               const uint8_t* tx, uint8_t* rx,
                               size_t len, uint32_t timeout_ms)
{
    SPI_TypeDef*               spi;
    bus_dma_xfer_t             rx_cfg = {0};
    bus_dma_xfer_t             tx_cfg = {0};
    const uint8_t*             tx_buf = tx;
    uint8_t*                   rx_buf = rx;
    int                        ret;
    uint8_t                    sem_storage[OSAL_SEM_STORAGE_SIZE];

    if (!host || !dma_tx || !dma_rx || len == 0)
        return VFS_ERR_INVAL;

    if (!host->spi)
        return VFS_ERR_IO;

    if (len > HAL_SPI_MAX_XFER)
        return VFS_ERR_INVAL;

    spi = (SPI_TypeDef*)host->spi;

    if (osal_sem_create_binary_static(&host->sync_sem, sem_storage, sizeof(sem_storage)) != 0)
        return VFS_ERR_NOMEM;

    if (!tx_buf)
    {
        __builtin_memset(s_dummy_tx[host->hw_idx], 0xFF, len);
        tx_buf = s_dummy_tx[host->hw_idx];
    }
    if (!rx_buf)
    {
        rx_buf = s_dummy_rx[host->hw_idx];
    }

    bus_dma_set_callback(dma_rx, hal_spi_dma_isr, host);

    rx_cfg.src     = (const void*)&spi->DR;
    rx_cfg.dst     = rx_buf;
    rx_cfg.len     = len;
    rx_cfg.dir     = BUS_DMA_DIR_PERIPH_TO_MEM;
    rx_cfg.width   = BUS_DMA_WIDTH_BYTE;
    rx_cfg.src_inc = BUS_DMA_INC_FIXED;
    rx_cfg.dst_inc = BUS_DMA_INC_INCREMENT;

    tx_cfg.src     = tx_buf;
    tx_cfg.dst     = (void*)&spi->DR;
    tx_cfg.len     = len;
    tx_cfg.dir     = BUS_DMA_DIR_MEM_TO_PERIPH;
    tx_cfg.width   = BUS_DMA_WIDTH_BYTE;
    tx_cfg.src_inc = BUS_DMA_INC_INCREMENT;
    tx_cfg.dst_inc = BUS_DMA_INC_FIXED;

    ret = bus_dma_submit(dma_rx, &rx_cfg);
    if (ret != VFS_OK)
        goto out;

    LL_SPI_EnableDMAReq_RX(spi);

    ret = bus_dma_submit(dma_tx, &tx_cfg);
    if (ret != VFS_OK)
        goto out;

    LL_SPI_EnableDMAReq_TX(spi);

    if (osal_sem_wait(host->sync_sem, timeout_ms) != 0)
    {
        ret = VFS_ERR_TIMEOUT;
        goto out;
    }

    hal_spi_wait_idle(spi, timeout_ms);
    ret = VFS_OK;

out:
    LL_SPI_DisableDMAReq_TX(spi);
    LL_SPI_DisableDMAReq_RX(spi);
    COMPAT_IGNORE_RESULT(bus_dma_abort(dma_tx));
    COMPAT_IGNORE_RESULT(bus_dma_abort(dma_rx));
    osal_sem_destroy(host->sync_sem);
    host->sync_sem = NULL;
    return ret;
}

/**
 * @brief 中止 SPI DMA 传输: 关 DMA 请求 + abort DMA 通道
 * @param host   Host 对象指针
 * @param dma_tx TX DMA 通道 (可 NULL)
 * @param dma_rx RX DMA 通道 (可 NULL)
 */
void hal_spi_abort_stm32(struct hal_spi_bus_host* host,
                         struct bus_dma_chan* dma_tx,
                         struct bus_dma_chan* dma_rx)
{
    SPI_TypeDef* spi;

    if (!host || !host->spi)
        return;

    spi = (SPI_TypeDef*)host->spi;
    LL_SPI_DisableDMAReq_TX(spi);
    LL_SPI_DisableDMAReq_RX(spi);

    if (dma_tx)
        COMPAT_IGNORE_RESULT(bus_dma_abort(dma_tx));
    if (dma_rx)
        COMPAT_IGNORE_RESULT(bus_dma_abort(dma_rx));
}

/*============================================================================*/
/*                              DMA 强制中止 (panic/reboot 路径, 空实现)       */
/*============================================================================*/
/* STM32 hal_dma_force_stop 直接 abort DMA stream, 不需 SPI 介入, 空实现满足统一头。 */
void hal_spi_dma_abort(void)
{
}

/*============================================================================*/
/*                              异步传输 (STM32 不支持, 返回 NOTSUPP)          */
/*============================================================================*/
int hal_spi_transfer_async(struct hal_spi_dev* dev,
                           const uint8_t* tx, uint8_t* rx,
                           size_t len, hal_spi_callback_t cb,
                           void* userdata)
{
    COMPAT_IGNORE_RESULT(dev);
    COMPAT_IGNORE_RESULT(tx);
    COMPAT_IGNORE_RESULT(rx);
    COMPAT_IGNORE_RESULT(len);
    COMPAT_IGNORE_RESULT(cb);
    COMPAT_IGNORE_RESULT(userdata);
    return VFS_ERR_NOTSUPP;
}

int hal_spi_transfer_poll(struct hal_spi_dev* dev, uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(dev);
    COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}

int hal_spi_get_trans_result(struct hal_spi_dev* dev, uint8_t* rx_data, size_t rx_cap,
                             size_t* trans_len, uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(dev);
    COMPAT_IGNORE_RESULT(rx_data);
    COMPAT_IGNORE_RESULT(rx_cap);
    COMPAT_IGNORE_RESULT(trans_len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}

/*============================================================================*/
/*                              Slave 传输 (STM32 不支持, 返回 NOTSUPP)        */
/*============================================================================*/
int spi_slave_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
                   size_t len, uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(dev);
    COMPAT_IGNORE_RESULT(tx);
    COMPAT_IGNORE_RESULT(rx);
    COMPAT_IGNORE_RESULT(len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}

int spi_slave_queue_tx(struct hal_spi_dev* dev, const uint8_t* data, size_t len,
                       uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(dev);
    COMPAT_IGNORE_RESULT(data);
    COMPAT_IGNORE_RESULT(len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}
