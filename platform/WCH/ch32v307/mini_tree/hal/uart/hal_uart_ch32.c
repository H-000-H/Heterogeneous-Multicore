/* SPDX-License-Identifier: Apache-2.0 */
/*
 * UART HAL — CH32V307 实现
 *
 * 设计: 硬件直投, DTSI 厂商宏值零翻译透传给 WCH 标准外设库。
 * - hal_uart_dev 嵌入 bus 层, HAL 无池管理无 vtable
 * - 自行配置 GPIO (TX=GPIO_Mode_AF_PP, RX=GPIO_Mode_IN_FLOATING), 不依赖 MounRiver MX;
 *   write/read 用 USART 轮询, fast path 访问 dev->uart
 */
#include "hal_uart.h"
#include "hal_dma_ch32.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include "ch32v30x_usart.h"
#include "ch32v30x_gpio.h"
#include "ch32v30x_rcc.h"
#include "ch32v30x.h"

#include "dt_config_gen.h"

/* ── 平台参数：来自 DTS ch32,uart-platform-cap，无 DTS 时提供回退 ── */
#ifndef DTC_GEN_CH32_UART_HOST_MAX
#define DTC_GEN_CH32_UART_HOST_MAX  1
#endif
#ifndef DTC_GEN_CH32_UART_MAX_XFER
#define DTC_GEN_CH32_UART_MAX_XFER  512U
#endif
#ifndef DTC_GEN_CH32_UART_TIMEOUT_MS
#define DTC_GEN_CH32_UART_TIMEOUT_MS  10U
#endif

#define CH32_UART_DMA_MAX_XFER      DTC_GEN_CH32_UART_MAX_XFER
#define CH32_UART_READ_TIMEOUT_MS   DTC_GEN_CH32_UART_TIMEOUT_MS
#define CH32_USART1_DR_ADDR         ((uint32_t)&USART1->DATAR)

/*============================================================================*/
/*                              WCH 标准外设库直投 helper                      */
/*============================================================================*/
/**
 * @brief 使能 UART 外设时钟 (USART1 挂 APB2, 其余 USART/UART 挂 APB1)
 * @param uart_clk_periph UART 时钟外设位 (RCC_APB2Periph_USART1 或 RCC_APB1Periph_USARTx)
 */
static void hal_uart_enable_usart_clk(uint32_t uart_clk_periph)
{
    /* USART1 挂 APB2, 其余 USART/UART 挂 APB1 — 由 DTSI 提供的时钟位决定总线 */
    if (uart_clk_periph == RCC_APB2Periph_USART1)
        RCC_APB2PeriphClockCmd(uart_clk_periph, ENABLE);
    else
        RCC_APB1PeriphClockCmd(uart_clk_periph, ENABLE);
}

/**
 * @brief 配置 UART 复用引脚: 时钟使能 + GPIO 模式 (WCH 标准外设库直投)
 * @param pin 引脚配置 (含 port/pin/clk_periph/af, af 承载 GPIOMode_TypeDef)
 */
static void hal_uart_config_af_pin(const struct hal_uart_pin_cfg* pin)
{
    GPIO_InitTypeDef  gpio;
    GPIO_TypeDef*     port = (GPIO_TypeDef*)pin->port;

    /* GPIO 永远挂 APB2 */
    RCC_APB2PeriphClockCmd(pin->clk_periph, ENABLE);

    __builtin_memset(&gpio, 0, sizeof(gpio));
    gpio.GPIO_Pin   = pin->pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = (GPIOMode_TypeDef)pin->af;  /* TX: AF_PP, RX: IN_FLOATING */
    GPIO_Init(port, &gpio);
}

/**
 * @brief 轮询等待 UART TC (发送完成) 标志置位
 * @param usart      UART 外设寄存器基址
 * @param timeout_ms 超时 (ms)
 * @return 成功返回 VFS_OK, 超时返回 VFS_ERR_TIMEOUT
 */
static int ch32_usart_wait_tc(USART_TypeDef* usart, uint32_t timeout_ms)
{
    uint32_t start = osal_time_ms();
    while (USART_GetFlagStatus(usart, USART_FLAG_TC) == RESET)
    {
        if ((uint32_t)(osal_time_ms() - start) >= timeout_ms)
            return VFS_ERR_TIMEOUT;
    }
    return VFS_OK;
}

/*============================================================================*/
/*                              Device 管理 API                               */
/*============================================================================*/
/**
 * @brief UART Device 对象初始化: 清零 + 拷贝配置 + 缓存 uart + 标记 UNINIT
 * @param dev      Device 对象指针
 * @param pool_idx 设备在 bus 层池中的索引
 * @param cfg      UART 配置 (DTSI 厂商宏值)
 */
void hal_uart_dev_init(struct hal_uart_dev* dev, int pool_idx,
                       const struct hal_uart_config* cfg)
{
    __builtin_memset(dev, 0, sizeof(*dev));
    dev->cfg      = *cfg;
    dev->pool_idx = pool_idx;
    dev->status   = UART_STATE_UNINIT;
}

/**
 * @brief 打开 UART 硬件: 使能时钟 + 配置 TX/RX GPIO + USART_Init + 使能 USART + 缓存 uart
 * @param dev Device 对象指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL
 */
int hal_uart_dev_hw_open(struct hal_uart_dev* dev)
{
    USART_InitTypeDef init = {0};
    USART_TypeDef*    uart;

    if (!dev || !dev->cfg.uart)
        return VFS_ERR_INVAL;
    if (dev->hw_inited)
        return VFS_OK;

    uart = (USART_TypeDef*)dev->cfg.uart;

    hal_uart_enable_usart_clk(dev->cfg.uart_clk_periph);
    hal_uart_config_af_pin(&dev->cfg.tx);
    hal_uart_config_af_pin(&dev->cfg.rx);

    __builtin_memset(&init, 0, sizeof(init));
    init.USART_BaudRate            = dev->cfg.baud_rate;
    init.USART_WordLength          = (uint16_t)dev->cfg.data_width;
    init.USART_StopBits            = (uint16_t)dev->cfg.stop_bits;
    init.USART_Parity              = (uint16_t)dev->cfg.parity;
    init.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    init.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(uart, &init);
    USART_Cmd(uart, ENABLE);

    /* 缓存 fast path 字段 */
    dev->uart      = dev->cfg.uart;
    dev->hw_inited = true;
    dev->status    = UART_STATE_READY;
    return VFS_OK;
}

/**
 * @brief 关闭 UART 硬件: 禁用 USART + 标记 UNINIT
 * @param dev Device 对象指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL
 */
int hal_uart_dev_hw_close(struct hal_uart_dev* dev)
{
    if (!dev || !dev->uart)
        return VFS_ERR_INVAL;

    USART_Cmd((USART_TypeDef*)dev->uart, DISABLE);
    dev->hw_inited = false;
    dev->status    = UART_STATE_UNINIT;
    return VFS_OK;
}

/*============================================================================*/
/*                              同步传输                                       */
/*============================================================================*/
/**
 * @brief UART 同步写: 逐字节 TXE 轮询发送 + 等待 TC 完成
 * @param dev  Device 对象指针
 * @param data 待发送数据
 * @param len  字节数
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 超时返回 VFS_ERR_TIMEOUT, 外设异常返回 VFS_ERR_IO
 */
int hal_uart_write(struct hal_uart_dev* dev, const uint8_t* data, size_t len)
{
    USART_TypeDef* usart;
    uint32_t       start;
    size_t         i;

    if (!dev || !data || len == 0)
        return VFS_ERR_INVAL;

    usart = (USART_TypeDef*)dev->uart;
    if (!usart)
        return VFS_ERR_IO;

    dev->status = UART_STATE_BUSY;
    start = osal_time_ms();
    for (i = 0; i < len; i++)
    {
        while (USART_GetFlagStatus(usart, USART_FLAG_TXE) == RESET)
        {
            if ((uint32_t)(osal_time_ms() - start) >= CH32_UART_READ_TIMEOUT_MS)
            {
                dev->status = UART_STATE_ERROR;
                return VFS_ERR_TIMEOUT;
            }
        }
        USART_SendData(usart, data[i]);
    }

    int ret = ch32_usart_wait_tc(usart, CH32_UART_READ_TIMEOUT_MS);
    dev->status = (ret == VFS_OK) ? UART_STATE_READY : UART_STATE_ERROR;
    return ret;
}

/**
 * @brief UART 同步读: 逐字节 RXNE 轮询接收, 超时已读到部分则返回已读字节数
 * @param dev  Device 对象指针
 * @param data 接收缓冲区
 * @param len  字节数
 * @return 成功返回读到的字节数, 参数非法返回 VFS_ERR_INVAL, 一字节未读到且超时返回 VFS_ERR_TIMEOUT, 外设异常返回 VFS_ERR_IO
 */
int hal_uart_read(struct hal_uart_dev* dev, uint8_t* data, size_t len)
{
    USART_TypeDef* usart;
    uint32_t       start;
    size_t         i;

    if (!dev || !data || len == 0)
        return VFS_ERR_INVAL;

    usart = (USART_TypeDef*)dev->uart;
    if (!usart)
        return VFS_ERR_IO;

    dev->status = UART_STATE_BUSY;
    start = osal_time_ms();
    for (i = 0; i < len; i++)
    {
        while (USART_GetFlagStatus(usart, USART_FLAG_RXNE) == RESET)
        {
            if ((uint32_t)(osal_time_ms() - start) >= CH32_UART_READ_TIMEOUT_MS)
            {
                dev->status = UART_STATE_ERROR;
                return (i > 0) ? (int)i : VFS_ERR_TIMEOUT;
            }
        }
        data[i] = (uint8_t)USART_ReceiveData(usart);
    }

    dev->status = UART_STATE_READY;
    return (int)len;
}

/**
 * @brief 强制停止 USART1 (panic/reboot 路径使用)
 * @return 成功返回 VFS_OK
 */
int hal_uart_force_stop(void)
{
    USART_Cmd(USART1, DISABLE);
    return VFS_OK;
}

/*============================================================================*/
/*                              DMA 传输 (保留原接口, 供 bus 层选用)           */
/*============================================================================*/
/**
 * @brief 强行中止 UART DMA: 关 DMA 请求 + 禁用 DMA1_Channel4
 * @param usart UART 外设寄存器基址
 */
static void ch32_uart_dma_abort(USART_TypeDef* usart)
{
    USART_DMACmd(usart, USART_DMAReq_Tx, DISABLE);
    (void)hal_dma_ch32_channel_disable(DMA1_Channel4, HAL_DMA_XFER_TIMEOUT_MS);
}

/**
 * @brief UART DMA 写: 配置 DMA1_Channel4 + 轮询 TC 标志 + 等 TC 完成 (保留接口, 供 bus 层选用)
 * @param pdev       Device 对象指针
 * @param dma_tx     TX DMA 通道 (CH32 内部固定 DMA1_Channel4, 此参数未使用)
 * @param data       待发送数据
 * @param len        字节数
 * @param timeout_ms 超时 (ms, 当前实现内部用 HAL_DMA_XFER_TIMEOUT_MS)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 外设异常返回 VFS_ERR_IO
 */
int hal_uart_write_dma(struct hal_uart_dev* pdev,
                             struct bus_dma_chan* dma_tx,
                             const uint8_t* data, size_t len,
                             uint32_t timeout_ms)
{
    USART_TypeDef*      usart;
    hal_dma_ch32_xfer_t cfg;
    int                 ret;

    COMPAT_IGNORE_RESULT(dma_tx); COMPAT_IGNORE_RESULT(timeout_ms);

    if (!pdev || !data || len == 0 || len > CH32_UART_DMA_MAX_XFER)
        return VFS_ERR_INVAL;

    usart = (USART_TypeDef*)pdev->uart;
    if (!usart)
        return VFS_ERR_IO;

    hal_dma_ch32_clocks_enable();

    cfg.channel     = DMA1_Channel4;
    cfg.tc_flag     = DMA1_FLAG_TC4;
    cfg.te_flag     = DMA1_FLAG_TE4;
    cfg.periph_addr = CH32_USART1_DR_ADDR;
    cfg.mem_addr    = (uint32_t)data;
    cfg.dir         = DMA_DIR_PeripheralDST;
    cfg.len         = (uint16_t)len;

    ret = hal_dma_ch32_channel_setup(&cfg);
    if (ret == VFS_OK)
    {
        (void)hal_dma_ch32_channel_enable(DMA1_Channel4);
        USART_DMACmd(usart, USART_DMAReq_Tx, ENABLE);
        ret = hal_dma_ch32_channel_poll(DMA1_FLAG_TC4, DMA1_FLAG_TE4, HAL_DMA_XFER_TIMEOUT_MS);
        if (ret == VFS_OK)
            ret = ch32_usart_wait_tc(usart, HAL_DMA_XFER_TIMEOUT_MS);
    }

    ch32_uart_dma_abort(usart);
    return ret;
}

/**
 * @brief 强行中止 USART1 上的 DMA (panic/reboot 路径全局入口, hal_dma_ch32 调用)
 */
void hal_uart_dma_abort(struct hal_uart_dev* pdev, struct bus_dma_chan* dma_tx)
{
    COMPAT_IGNORE_RESULT(pdev);
    COMPAT_IGNORE_RESULT(dma_tx);
    ch32_uart_dma_abort(USART1);
}
