/* SPDX-License-Identifier: Apache-2.0 */
/*
 * SPI HAL — CH32V307 实现 (Master only)
 *
 * 设计: 硬件直投, DTSI 厂商宏值零翻译透传给标准外设库。
 * - hal_spi_bus_host 嵌入 bus 层, HAL 无 s_spi_hosts[] 池
 * - spi_sync: CS 变更检测用 cs_port + cs_pin 直接比较 (多设备共线不打架)
 * - slave / async 返回 VFS_ERR_NOTSUPP
 */
#include "hal_spi.h"
#include "hal_dma_ch32.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include "ch32v30x_spi.h"
#include "ch32v30x.h"

#include "dt_config_gen.h"

#ifndef DTC_GEN_CH32_SPI_HOST_MAX
#define DTC_GEN_CH32_SPI_HOST_MAX  1
#endif
#ifndef DTC_GEN_CH32_SPI_MAX_XFER
#define DTC_GEN_CH32_SPI_MAX_XFER  512U
#endif

#define HAL_SPI_HOST_MAX  DTC_GEN_CH32_SPI_HOST_MAX
#define HAL_SPI_MAX_XFER  DTC_GEN_CH32_SPI_MAX_XFER
#define CH32_SPI1_DR_ADDR   ((uint32_t)&SPI1->DATAR)

/* dummy buffer: DMA 路径下 tx/rx 为 NULL 时填充占位。
 * - s_dummy_tx 填 0xFF: 用户只收时, DMA 仍需往 SPI->DATAR 写驱动 SCLK
 * - s_dummy_rx 丢弃区:  用户只发时, DMA 仍需从 SPI->DATAR 读避免 OVR
 * 32 字节对齐适配 DMA; WCH 单 host 无需 per-host 索引。 */
static uint8_t s_dummy_tx[HAL_SPI_MAX_XFER] COMPAT_ALIGNED(32);
static uint8_t s_dummy_rx[HAL_SPI_MAX_XFER] COMPAT_ALIGNED(32);

/* 前向声明: spi_sync 按 dma-chan 分派调 hal_spi_transfer_dma_ch32,
 * 后者内部调 ch32_spi_dma_abort (静态 helper, 定义在 DMA 段)。 */
static void ch32_spi_dma_abort(SPI_TypeDef* spi);
int hal_spi_transfer_dma_ch32(struct hal_spi_bus_host* host,
                              struct bus_dma_chan* dma_tx,
                              struct bus_dma_chan* dma_rx,
                              const uint8_t* tx, uint8_t* rx,
                              size_t len, uint32_t timeout_ms);

/*============================================================================*/
/*                              标准外设库直投 helper                          */
/*============================================================================*/
/* WCH 的 GPIOMode_TypeDef 已把 mode+af 编码在一起 (GPIO_Mode_AF_PP), 直接透传 */
/**
 * @brief 配置 SPI 复用引脚: 时钟使能 + AF_PP 模式 (标准外设库直投)
 * @param pin 引脚配置 (含 port/pin/clk_periph/mode, mode 承载 GPIOMode_TypeDef)
 */
static void hal_spi_config_af_pin(const struct hal_spi_pin_cfg* pin)
{
    GPIO_InitTypeDef init;
    GPIO_TypeDef*    port;

    RCC_APB2PeriphClockCmd(pin->clk_periph, ENABLE);

    port = (GPIO_TypeDef*)pin->port;
    init.GPIO_Pin   = pin->pin;
    init.GPIO_Speed = GPIO_Speed_50MHz;
    init.GPIO_Mode  = (GPIOMode_TypeDef)pin->af;
    GPIO_Init(port, &init);
}

/**
 * @brief 复位 SPI 复用引脚为浮空输入 (等效去初始化)
 * @param pin 引脚配置
 */
static void hal_spi_reset_af_pin(const struct hal_spi_pin_cfg* pin)
{
    GPIO_InitTypeDef init;
    GPIO_TypeDef*    port = (GPIO_TypeDef*)pin->port;

    init.GPIO_Pin  = pin->pin;
    init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(port, &init);
}

/**
 * @brief 由目标时钟频率选最近档位的标准外设库波特率预分频值
 * @param clock_hz 目标时钟频率 (Hz), <=0 时返回最大分频
 * @return SPI_BaudRatePrescaler_* 宏值
 */
static uint16_t ch32_spi_prescaler(int clock_hz)
{
    if (clock_hz <= 0) return SPI_BaudRatePrescaler_256;
    if (clock_hz >= 21000000) return SPI_BaudRatePrescaler_2;
    if (clock_hz >= 10500000) return SPI_BaudRatePrescaler_4;
    if (clock_hz >= 5250000)  return SPI_BaudRatePrescaler_8;
    if (clock_hz >= 2625000)  return SPI_BaudRatePrescaler_16;
    if (clock_hz >= 1312500)  return SPI_BaudRatePrescaler_32;
    if (clock_hz >= 656250)   return SPI_BaudRatePrescaler_64;
    if (clock_hz >= 328125)   return SPI_BaudRatePrescaler_128;
    return SPI_BaudRatePrescaler_256;
}

/**
 * @brief 清空 SPI RXNE 残留数据, 防止误读历史字节
 * @param spi SPI 外设寄存器基址
 */
static void ch32_spi_clear_errors(SPI_TypeDef* spi)
{
    while (SPI_I2S_GetFlagStatus(spi, SPI_I2S_FLAG_RXNE) != RESET)
        (void)SPI_I2S_ReceiveData(spi);
}

/**
 * @brief 轮询等待 SPI BSY 标志清零
 * @param spi        SPI 外设寄存器基址
 * @param timeout_ms 超时 (ms)
 * @return 成功返回 VFS_OK, 超时返回 VFS_ERR_TIMEOUT
 */
static int ch32_spi_wait_idle(SPI_TypeDef* spi, uint32_t timeout_ms)
{
    uint32_t start = osal_time_ms();
    while (SPI_I2S_GetFlagStatus(spi, SPI_I2S_FLAG_BSY) != RESET)
    {
        if ((uint32_t)(osal_time_ms() - start) >= timeout_ms)
            return VFS_ERR_TIMEOUT;
    }
    return VFS_OK;
}

/*============================================================================*/
/*                              Host 管理 API                                 */
/*============================================================================*/
/**
 * @brief SPI Host 初始化: 使能 SPI 时钟 + 配置 MOSI/MISO/SCLK 引脚 + 缓存 fast path 字段
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

    RCC_APB2PeriphClockCmd(cfg->spi_clk_periph, ENABLE);

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
 * @brief SPI Host 反初始化: 关闭 SPI + 复位 MOSI/MISO/SCLK 为浮空输入
 * @param host Host 对象指针
 * @return 成功返回 VFS_OK, host 为空返回 VFS_ERR_INVAL, 未就绪直接返回 VFS_OK
 */
int hal_spi_bus_host_deinit(struct hal_spi_bus_host* host)
{
    if (!host)
        return VFS_ERR_INVAL;
    if (!host->bus_ready)
        return VFS_OK;

    SPI_Cmd((SPI_TypeDef*)host->spi, DISABLE);
    /* 引脚复位为浮空输入 (同 GPIO deinit 模式) */
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
 * @brief 将 device 配置应用到 SPI 硬件: 关 SPI → 填 SPI_InitTypeDef → 重启 SPI
 * @param host     Host 对象指针
 * @param dev_cfg  设备配置 (mode/clock_speed_hz)
 * @return 成功返回 VFS_OK, 参数非法或 host->spi 为空返回 VFS_ERR_IO
 */
static int ch32_spi_apply_dev_cfg(struct hal_spi_bus_host* host,
                                   const struct hal_spi_device_config* dev_cfg)
{
    SPI_InitTypeDef init = {0};
    SPI_TypeDef*    spi;

    if (!host || !host->spi || !dev_cfg)
        return VFS_ERR_IO;

    spi = (SPI_TypeDef*)host->spi;
    SPI_Cmd(spi, DISABLE);
    init.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    init.SPI_Mode              = SPI_Mode_Master;
    init.SPI_DataSize          = SPI_DataSize_8b;
    init.SPI_CPOL              = (dev_cfg->mode & 2) ? SPI_CPOL_High : SPI_CPOL_Low;
    init.SPI_CPHA              = (dev_cfg->mode & 1) ? SPI_CPHA_2Edge : SPI_CPHA_1Edge;
    init.SPI_NSS               = SPI_NSS_Soft;
    init.SPI_BaudRatePrescaler = ch32_spi_prescaler(dev_cfg->clock_speed_hz);
    init.SPI_FirstBit          = SPI_FirstBit_MSB;
    init.SPI_CRCPolynomial     = 7;
    SPI_Init(spi, &init);
    SPI_Cmd(spi, ENABLE);
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

    ret = ch32_spi_apply_dev_cfg(host, &dev->cfg);
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
 * @brief SPI 轮询传输: 逐字节 TXE/RXNE 标志轮询, 超时检测基于 osal_time_ms
 * @param host        Host 对象指针
 * @param tx          发送缓冲区 (可为 NULL, 内部填 0xFF)
 * @param rx          接收缓冲区 (可为 NULL, 仅丢弃)
 * @param len         传输字节数
 * @param timeout_ms  超时 (ms, 当前实现未做超时检测)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 外设异常返回 VFS_ERR_IO
 */
static int ch32_spi_transfer_poll(struct hal_spi_bus_host* host,
                                   const uint8_t* tx, uint8_t* rx,
                                   size_t len, uint32_t timeout_ms)
{
    SPI_TypeDef*              spi;
    uint32_t                  start;
    size_t                    i;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!host || len == 0)
        return VFS_ERR_INVAL;

    if (!host->spi)
        return VFS_ERR_IO;

    if (len > HAL_SPI_MAX_XFER)
        return VFS_ERR_INVAL;

    spi   = (SPI_TypeDef*)host->spi;
    start = osal_time_ms();

    for (i = 0; i < len; i++)
    {
        uint8_t out = tx ? tx[i] : 0xFFU;

        while (SPI_I2S_GetFlagStatus(spi, SPI_I2S_FLAG_TXE) == RESET)
        {
            if ((uint32_t)(osal_time_ms() - start) >= timeout_ms)
                return VFS_ERR_TIMEOUT;
        }
        SPI_I2S_SendData(spi, out);
        while (SPI_I2S_GetFlagStatus(spi, SPI_I2S_FLAG_RXNE) == RESET)
        {
            if ((uint32_t)(osal_time_ms() - start) >= timeout_ms)
                return VFS_ERR_TIMEOUT;
        }
        if (rx)
            rx[i] = (uint8_t)SPI_I2S_ReceiveData(spi);
        else
            (void)SPI_I2S_ReceiveData(spi);
    }

    return VFS_OK;
}

/**
 * @brief SPI 同步传输 (Master): 配置变更检测后转 ch32_spi_transfer_poll 执行
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
        dev->ctlr->active_cfg.cs_pin     != dev->cfg.cs_pin     ||
        dev->ctlr->active_cfg.mode        != dev->cfg.mode        ||
        dev->ctlr->active_cfg.clock_speed_hz != dev->cfg.clock_speed_hz)
    {
        int ret = ch32_spi_apply_dev_cfg(dev->ctlr, &dev->cfg);
        if (ret != VFS_OK)
            return ret;
        dev->ctlr->active_cfg = dev->cfg;
    }

    /* 按 dma-chan 分派: >=0 走 DMA, <0 走轮询。
     * WCH DMA 函数自管 DMA1_Channel2/3, 不依赖 bus_dma_chan* 参数。 */
    if (dev->ctlr->cfg.dma_chan >= 0)
        return hal_spi_transfer_dma_ch32(dev->ctlr, NULL, NULL, tx, rx, len, timeout_ms);

    return ch32_spi_transfer_poll(dev->ctlr, tx, rx, len, timeout_ms);
}

/*============================================================================*/
/*                              DMA 传输 (自管 DMA1_Channel2/3)                */
/*============================================================================*/
/**
 * @brief SPI DMA 同步传输: 自管 DMA1_Channel2(RX)/Channel3(TX), 内部轮询 TC 标志
 * @param host       Host 对象指针
 * @param dma_tx     未使用 (WCH 自管 DMA 通道, 保留参数为接口兼容)
 * @param dma_rx     未使用 (同上)
 * @param tx         发送缓冲 (NULL 时用 s_dummy_tx 填 0xFF)
 * @param rx         接收缓冲 (NULL 时用 s_dummy_rx 丢弃)
 * @param len        字节数
 * @param timeout_ms 未使用 (内部用 HAL_DMA_XFER_TIMEOUT_MS)
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_*
 */
int hal_spi_transfer_dma_ch32(struct hal_spi_bus_host* host,
                               struct bus_dma_chan* dma_tx,
                               struct bus_dma_chan* dma_rx,
                               const uint8_t* tx, uint8_t* rx,
                               size_t len, uint32_t timeout_ms)
{
    SPI_TypeDef*              spi;
    const uint8_t*            tx_buf;
    uint8_t*                  rx_buf;
    hal_dma_ch32_xfer_t       rx_cfg;
    hal_dma_ch32_xfer_t       tx_cfg;
    int                       ret;

    COMPAT_IGNORE_RESULT(dma_tx); COMPAT_IGNORE_RESULT(dma_rx); COMPAT_IGNORE_RESULT(timeout_ms);

    if (!host || len == 0)
        return VFS_ERR_INVAL;

    if (!host->spi)
        return VFS_ERR_IO;

    if (len > HAL_SPI_MAX_XFER)
        return VFS_ERR_INVAL;

    spi = (SPI_TypeDef*)host->spi;

    hal_dma_ch32_clocks_enable();

    tx_buf = tx;
    rx_buf = rx;
    if (!tx_buf)
    {
        __builtin_memset(s_dummy_tx, 0xFF, len);
        tx_buf = s_dummy_tx;
    }
    if (!rx_buf)
        rx_buf = s_dummy_rx;

    ch32_spi_clear_errors(spi);
    SPI_Cmd(spi, ENABLE);

    rx_cfg.channel     = DMA1_Channel2;
    rx_cfg.tc_flag     = DMA1_FLAG_TC2;
    rx_cfg.te_flag     = DMA1_FLAG_TE2;
    rx_cfg.periph_addr = CH32_SPI1_DR_ADDR;
    rx_cfg.mem_addr    = (uint32_t)rx_buf;
    rx_cfg.dir         = DMA_DIR_PeripheralSRC;
    rx_cfg.len         = (uint16_t)len;

    tx_cfg.channel     = DMA1_Channel3;
    tx_cfg.tc_flag     = DMA1_FLAG_TC3;
    tx_cfg.te_flag     = DMA1_FLAG_TE3;
    tx_cfg.periph_addr = CH32_SPI1_DR_ADDR;
    tx_cfg.mem_addr    = (uint32_t)tx_buf;
    tx_cfg.dir         = DMA_DIR_PeripheralDST;
    tx_cfg.len         = (uint16_t)len;

    ret = hal_dma_ch32_channel_setup(&rx_cfg);
    if (ret != VFS_OK)
        goto out;

    ret = hal_dma_ch32_channel_setup(&tx_cfg);
    if (ret != VFS_OK)
        goto out;

    (void)hal_dma_ch32_channel_enable(DMA1_Channel2);
    (void)hal_dma_ch32_channel_enable(DMA1_Channel3);
    SPI_I2S_DMACmd(spi, SPI_I2S_DMAReq_Rx | SPI_I2S_DMAReq_Tx, ENABLE);

    ret = hal_dma_ch32_channel_poll(DMA1_FLAG_TC3, DMA1_FLAG_TE3, HAL_DMA_XFER_TIMEOUT_MS);
    if (ret != VFS_OK)
        goto out;

    ret = ch32_spi_wait_idle(spi, HAL_DMA_XFER_TIMEOUT_MS);

out:
    ch32_spi_dma_abort(spi);
    return ret;
}

/**
 * @brief 中止 SPI DMA 传输 (转 ch32_spi_dma_abort 静态 helper)
 * @param host Host 对象指针
 */
void hal_spi_abort_ch32(struct hal_spi_bus_host* host)
{
    if (!host || !host->spi)
        return;

    ch32_spi_dma_abort((SPI_TypeDef*)host->spi);
}

/*============================================================================*/
/*                              DMA 强制中止 (panic/reboot 路径)               */
/*============================================================================*/
/**
 * @brief 强行中止 SPI DMA: 关 DMA 请求 + 禁用 DMA1 通道 2/3 + 等空闲 + 清残留
 * @param spi SPI 外设寄存器基址
 */
static void ch32_spi_dma_abort(SPI_TypeDef* spi)
{
    SPI_I2S_DMACmd(spi, SPI_I2S_DMAReq_Tx | SPI_I2S_DMAReq_Rx, DISABLE);
    (void)hal_dma_ch32_channel_disable(DMA1_Channel2, HAL_DMA_XFER_TIMEOUT_MS);
    (void)hal_dma_ch32_channel_disable(DMA1_Channel3, HAL_DMA_XFER_TIMEOUT_MS);
    (void)ch32_spi_wait_idle(spi, HAL_DMA_XFER_TIMEOUT_MS);
    ch32_spi_clear_errors(spi);
}

/**
 * @brief 强行中止 SPI1 上的 DMA (panic/reboot 路径全局入口, hal_dma_force_stop 调用)
 */
void hal_spi_dma_abort(void)
{
    ch32_spi_dma_abort(SPI1);
}

/*============================================================================*/
/*                              异步传输 (CH32 不支持, 返回 NOTSUPP)           */
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
/*                              Slave 传输 (CH32 不支持, 返回 NOTSUPP)         */
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
