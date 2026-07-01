/* SPDX-License-Identifier: Apache-2.0 */
/*
 * UART HAL — STM32F4 实现
 *
 * 设计: 硬件直投, DTSI 厂商宏值零翻译透传给 LL 库。
 * - hal_uart_dev 嵌入 bus 层, HAL 无池管理无 vtable
 * - 自行配置 GPIO AF, 不依赖 CubeMX; write/read 用 LL_USART 轮询, fast path 访问 dev->uart
 */
#include "hal_uart.h"
#include "dma.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
#include "dt_config_gen.h"

/* ── 平台参数：来自 DTS stm32,uart-platform-cap，无 DTS 时提供回退 ── */
#ifndef DTC_GEN_STM32_UART_MAX_XFER
#define DTC_GEN_STM32_UART_MAX_XFER  512U
#endif
#ifndef DTC_GEN_STM32_UART_TIMEOUT_MS
#define DTC_GEN_STM32_UART_TIMEOUT_MS  10U
#endif

#define STM32_UART_DMA_MAX_XFER    DTC_GEN_STM32_UART_MAX_XFER
#define STM32_UART_READ_TIMEOUT_MS DTC_GEN_STM32_UART_TIMEOUT_MS

/*============================================================================*/
/*                              LL 库直投 helper                              */
/*============================================================================*/
/* 纯 LL 库调用, 非抽象层 */
/**
 * @brief 配置 UART 复用引脚: 时钟使能 + AF 模式 + 推挽高速 (LL 库直投)
 * @param pin 引脚配置 (含 port/pin/clk_periph/af)
 */
static void hal_uart_config_af_pin(const struct hal_uart_pin_cfg* pin)
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
 * @brief 轮询等待 UART TC (发送完成) 标志置位
 * @param usart      UART 外设寄存器基址
 * @param timeout_ms 超时 (ms)
 * @return 成功返回 VFS_OK, 超时返回 VFS_ERR_TIMEOUT
 */
static int stm32_uart_wait_tc(USART_TypeDef* usart, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while (!LL_USART_IsActiveFlag_TC(usart))
    {
        if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms)
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
void hal_uart_dev_init(struct hal_uart_dev* dev, int pool_idx,const struct hal_uart_config* cfg)
{
    __builtin_memset(dev, 0, sizeof(*dev));
    dev->cfg      = *cfg;
    dev->pool_idx = pool_idx;
    dev->status   = UART_STATE_UNINIT;
}

/**
 * @brief 打开 UART 硬件: 使能时钟 + 配置 TX/RX 复用 + LL_USART_Init + 缓存 uart + 标记 READY
 * @param dev Device 对象指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, LL_USART_Init 失败返回 VFS_ERR_IO
 */
int hal_uart_dev_hw_open(struct hal_uart_dev* dev)
{
    LL_USART_InitTypeDef init = {0};
    USART_TypeDef*       uart;

    if (!dev || !dev->cfg.uart)
        return VFS_ERR_INVAL;
    if (dev->hw_inited)
        return VFS_OK;

    uart = (USART_TypeDef*)dev->cfg.uart;
    LL_APB1_GRP1_EnableClock(dev->cfg.uart_clk_periph);

    hal_uart_config_af_pin(&dev->cfg.tx);
    hal_uart_config_af_pin(&dev->cfg.rx);

    LL_USART_Disable(uart);

    init.BaudRate            = dev->cfg.baud_rate;
    init.DataWidth           = dev->cfg.data_width;
    init.StopBits            = dev->cfg.stop_bits;
    init.Parity              = dev->cfg.parity;
    init.TransferDirection   = LL_USART_DIRECTION_TX_RX;
    init.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
    init.OverSampling        = LL_USART_OVERSAMPLING_16;

    if (LL_USART_Init(uart, &init) != SUCCESS)
        return VFS_ERR_IO;

    LL_USART_ConfigAsyncMode(uart);
    LL_USART_Enable(uart);

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

    LL_USART_Disable((USART_TypeDef*)dev->uart);
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
    start = HAL_GetTick();
    for (i = 0; i < len; i++)
    {
        while (!LL_USART_IsActiveFlag_TXE(usart))
        {
            if ((uint32_t)(HAL_GetTick() - start) >= STM32_UART_READ_TIMEOUT_MS)
            {
                dev->status = UART_STATE_ERROR;
                return VFS_ERR_TIMEOUT;
            }
        }
        LL_USART_TransmitData8(usart, data[i]);
    }

    int ret = stm32_uart_wait_tc(usart, STM32_UART_READ_TIMEOUT_MS);
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
    start = HAL_GetTick();
    for (i = 0; i < len; i++)
    {
        while (!LL_USART_IsActiveFlag_RXNE(usart))
        {
            if ((uint32_t)(HAL_GetTick() - start) >= STM32_UART_READ_TIMEOUT_MS)
            {
                dev->status = UART_STATE_ERROR;
                return (i > 0) ? (int)i : VFS_ERR_TIMEOUT;
            }
        }
        data[i] = LL_USART_ReceiveData8(usart);
    }

    dev->status = UART_STATE_READY;
    return (int)len;
}

/**
 * @brief 强制停止所有 STM32 UART/USART (panic/reboot 路径使用)
 * @return 成功返回 VFS_OK
 */
int hal_uart_force_stop(void)
{
    LL_USART_Disable(USART1);
    LL_USART_Disable(USART2);
    LL_USART_Disable(USART3);
    LL_USART_Disable(UART4);
    LL_USART_Disable(UART5);
    LL_USART_Disable(USART6);
    return VFS_OK;
}

/*============================================================================*/
/*                              DMA 传输 (保留原接口, 供 bus 层选用)           */
/*============================================================================*/
struct hal_uart_dma_ctx {
    struct osal_sem* sync_sem;
    uint8_t          sem_storage[OSAL_SEM_STORAGE_SIZE];
};

/**
 * @brief UART DMA 完成 ISR 回调: 释放 ctx 同步信号量唤醒等待线程
 * @param chan      触发的 DMA 通道 (未使用)
 * @param user_data hal_uart_dma_ctx 指针 (用于取 sync_sem)
 */
static void hal_uart_dma_isr(struct bus_dma_chan* chan, void* user_data)
{
    struct hal_uart_dma_ctx* ctx = (struct hal_uart_dma_ctx*)user_data;
    (void)chan;
    if (ctx && ctx->sync_sem)
        COMPAT_IGNORE_RESULT(osal_sem_post_from_isr(ctx->sync_sem, NULL));
}

/**
 * @brief UART DMA 写: 提交 TX DMA + 等信号量 + 等 TC 完成 (保留接口, 供 bus 层选用)
 * @param pdev       Device 对象指针
 * @param dma_tx     TX DMA 通道
 * @param data       待发送数据
 * @param len        字节数
 * @param timeout_ms 超时 (ms)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 超时返回 VFS_ERR_TIMEOUT, 外设异常返回 VFS_ERR_IO, 信号量创建失败返回 VFS_ERR_NOMEM
 */
int hal_uart_write_dma(struct hal_uart_dev* pdev,
                              struct bus_dma_chan* dma_tx,
                              const uint8_t* data, size_t len,
                              uint32_t timeout_ms)
{
    USART_TypeDef*           usart;
    bus_dma_xfer_t           xfer = {0};
    struct hal_uart_dma_ctx  ctx;
    int                      ret;

    if (!pdev || !dma_tx || !data || len == 0)
        return VFS_ERR_INVAL;

    if (len > STM32_UART_DMA_MAX_XFER)
        return VFS_ERR_INVAL;

    usart = (USART_TypeDef*)pdev->uart;
    if (!usart)
        return VFS_ERR_IO;

    __builtin_memset(&ctx, 0, sizeof(ctx));
    if (osal_sem_create_binary_static(&ctx.sync_sem, ctx.sem_storage, sizeof(ctx.sem_storage)) != 0)
        return VFS_ERR_NOMEM;

    bus_dma_set_callback(dma_tx, hal_uart_dma_isr, &ctx);

    xfer.src     = data;
    xfer.dst     = (void*)&usart->DR;
    xfer.len     = len;
    xfer.dir     = BUS_DMA_DIR_MEM_TO_PERIPH;
    xfer.width   = BUS_DMA_WIDTH_BYTE;
    xfer.src_inc = BUS_DMA_INC_INCREMENT;
    xfer.dst_inc = BUS_DMA_INC_FIXED;

    LL_USART_ClearFlag_TC(usart);

    ret = bus_dma_submit(dma_tx, &xfer);
    if (ret != VFS_OK)
        goto out;

    LL_USART_EnableDMAReq_TX(usart);

    if (osal_sem_wait(ctx.sync_sem, timeout_ms) != 0)
    {
        ret = VFS_ERR_TIMEOUT;
        goto out;
    }

    ret = stm32_uart_wait_tc(usart, timeout_ms);

out:
    LL_USART_DisableDMAReq_TX(usart);
    COMPAT_IGNORE_RESULT(bus_dma_abort(dma_tx));
    osal_sem_destroy(ctx.sync_sem);
    return ret;
}

/**
 * @brief 强行中止 UART DMA: 关 DMA 请求 + abort TX 通道 (panic 路径使用)
 * @param pdev   Device 对象指针
 * @param dma_tx TX DMA 通道 (可为 NULL)
 */
void hal_uart_dma_abort(struct hal_uart_dev* pdev, struct bus_dma_chan* dma_tx)
{
    USART_TypeDef* usart;

    if (!pdev)
        return;

    usart = (USART_TypeDef*)pdev->uart;
    if (!usart)
        return;

    LL_USART_DisableDMAReq_TX(usart);

    if (dma_tx)
        COMPAT_IGNORE_RESULT(bus_dma_abort(dma_tx));
}
