/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * UART HAL — STM32F4 实现 (vtable 模式)
 *
 * 适配 ESP32 hal_uart.h vtable 架构, 保留 STM32 LL_USART 寄存器操作。
 * transmit (从机半双工) 返回 VFS_ERR_NOTSUPP。
 *
 * HAL 层不分配数据缓冲区。所有 tx/rx 指针由调用者提供。
 */
#include "hal_uart.h"
#include "dma.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"

#define STM32_UART_READ_TIMEOUT_MS  10U
#define STM32_UART_DMA_MAX_XFER      512U
#define STM32_UART_HOST_MAX          6   /* USART1/2/3 + UART4/5/6 */

struct hal_uart_stm32_priv {
    USART_TypeDef* usart;
};

static USART_TypeDef* stm32_usart_instance(int uart_host)
{
    switch (uart_host)
    {
    case 1: return USART1;
    case 2: return USART2;
    case 3: return USART3;
    case 4: return UART4;
    case 5: return UART5;
    case 6: return USART6;
    default: return NULL;
    }
}

static uint32_t stm32_uart_data_width(hal_uart_data_bits_t bits, hal_uart_parity_t parity)
{
    /* 9 位数据宽度当且仅当 8 数据位 + 校验 (奇/偶) */
    int wide9 = (bits == HAL_UART_DATA_BITS_8 && parity != HAL_UART_PARITY_NONE);
    return wide9 ? LL_USART_DATAWIDTH_9B : LL_USART_DATAWIDTH_8B;
}

static uint32_t stm32_uart_parity(hal_uart_parity_t parity)
{
    if (parity == HAL_UART_PARITY_EVEN) return LL_USART_PARITY_EVEN;
    if (parity == HAL_UART_PARITY_ODD)  return LL_USART_PARITY_ODD;
    return LL_USART_PARITY_NONE;
}

static uint32_t stm32_uart_stop_bits(hal_uart_stop_bits_t stop)
{
    if (stop == HAL_UART_STOP_BITS_2)   return LL_USART_STOPBITS_2;
    if (stop == HAL_UART_STOP_BITS_1_5) return LL_USART_STOPBITS_1_5;
    return LL_USART_STOPBITS_1;
}

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

/* ===== vtable 实现 ===== */
static int stm32_uart_open(const struct hal_uart_config_t* cfg)
{
    USART_TypeDef*      usart;
    LL_USART_InitTypeDef init = {0};

    if (!cfg)
        return VFS_ERR_INVAL;

    usart = stm32_usart_instance(cfg->uart_host);
    if (!usart)
        return VFS_ERR_NODEV;

    LL_USART_Disable(usart);

    init.BaudRate            = cfg->baud_rate;
    init.DataWidth           = stm32_uart_data_width(cfg->data_bits, cfg->parity);
    init.StopBits            = stm32_uart_stop_bits(cfg->stop_bits);
    init.Parity              = stm32_uart_parity(cfg->parity);
    init.TransferDirection   = LL_USART_DIRECTION_TX_RX;
    init.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
    init.OverSampling        = LL_USART_OVERSAMPLING_16;

    if (LL_USART_Init(usart, &init) != SUCCESS)
        return VFS_ERR_IO;

    LL_USART_ConfigAsyncMode(usart);
    LL_USART_Enable(usart);

    return VFS_OK;
}

static int stm32_uart_close(const struct hal_uart_config_t* cfg)
{
    USART_TypeDef* usart;

    if (!cfg)
        return VFS_ERR_INVAL;

    usart = stm32_usart_instance(cfg->uart_host);
    if (!usart)
        return VFS_ERR_NODEV;

    LL_USART_Disable(usart);
    return VFS_OK;
}

static int stm32_uart_write(struct hal_uart_dev* pdev, const uint8_t* data, size_t len)
{
    USART_TypeDef* usart;
    uint32_t       start;
    size_t         i;

    if (!pdev || !data || len == 0)
        return VFS_ERR_INVAL;

    usart = stm32_usart_instance(pdev->cfg.uart_host);
    if (!usart)
        return VFS_ERR_IO;

    start = HAL_GetTick();
    for (i = 0; i < len; i++)
    {
        while (!LL_USART_IsActiveFlag_TXE(usart))
        {
            if ((uint32_t)(HAL_GetTick() - start) >= STM32_UART_READ_TIMEOUT_MS)
                return VFS_ERR_TIMEOUT;
        }
        LL_USART_TransmitData8(usart, data[i]);
    }

    return stm32_uart_wait_tc(usart, STM32_UART_READ_TIMEOUT_MS);
}

static int stm32_uart_read(struct hal_uart_dev* pdev, uint8_t* data, size_t len)
{
    USART_TypeDef* usart;
    uint32_t       start;
    size_t         i;

    if (!pdev || !data || len == 0)
        return VFS_ERR_INVAL;

    usart = stm32_usart_instance(pdev->cfg.uart_host);
    if (!usart)
        return VFS_ERR_IO;

    start = HAL_GetTick();
    for (i = 0; i < len; i++)
    {
        while (!LL_USART_IsActiveFlag_RXNE(usart))
        {
            if ((uint32_t)(HAL_GetTick() - start) >= STM32_UART_READ_TIMEOUT_MS)
                return (i > 0) ? (int)i : VFS_ERR_TIMEOUT;
        }
        data[i] = LL_USART_ReceiveData8(usart);
    }

    return (int)len;
}

static int stm32_uart_transmit(struct hal_uart_dev* pdev, uint8_t* rx, uint8_t* tx,
                                size_t rx_len, size_t tx_len)
{
    /* 从机半双工模式不支持, 空实现 */
    (void)rx; (void)tx; (void)rx_len; (void)tx_len;
    (void)pdev;
    return VFS_ERR_NOTSUPP;
}

static int stm32_uart_deinit(const struct hal_uart_config_t* cfg)
{
    return stm32_uart_close(cfg);
}

static const struct hal_uart_bus s_stm32_uart_bus = {
    .open     = stm32_uart_open,
    .close    = stm32_uart_close,
    .read     = stm32_uart_read,
    .write    = stm32_uart_write,
    .transmit = stm32_uart_transmit,
    .deinit   = stm32_uart_deinit,
    ._impl    = NULL
};

const struct hal_uart_bus* hal_uart_bus_get(void)
{
    return &s_stm32_uart_bus;
}

int hal_uart_xfer_begin(struct hal_uart_dev* pdev, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!pdev)
        return VFS_ERR_INVAL;
    pdev->status = UART_STATE_BUSY;
    return VFS_OK;
}

int hal_uart_xfer_end(struct hal_uart_dev* pdev)
{
    if (!pdev)
        return VFS_ERR_INVAL;
    pdev->status = UART_STATE_READY;
    return VFS_OK;
}

int hal_uart_force_stop(void)
{
    /* 关闭所有 USART */
    LL_USART_Disable(USART1);
    LL_USART_Disable(USART2);
    LL_USART_Disable(USART3);
    LL_USART_Disable(UART4);
    LL_USART_Disable(UART5);
    LL_USART_Disable(USART6);
    return VFS_OK;
}

/* ===== DMA 传输 (保留原接口, 供 bus 层选用) ===== */
struct hal_uart_dma_ctx {
    struct osal_sem* sync_sem;
    uint8_t          sem_storage[OSAL_SEM_STORAGE_SIZE];
};

static void hal_uart_dma_isr(struct bus_dma_chan* chan, void* user_data)
{
    struct hal_uart_dma_ctx* ctx = (struct hal_uart_dma_ctx*)user_data;
    (void)chan;
    if (ctx && ctx->sync_sem)
        COMPAT_IGNORE_RESULT(osal_sem_post_from_isr(ctx->sync_sem, NULL));
}

int hal_uart_write_dma_stm32(struct hal_uart_dev* pdev,
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

    usart = stm32_usart_instance(pdev->cfg.uart_host);
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

void hal_uart_abort_stm32(struct hal_uart_dev* pdev, struct bus_dma_chan* dma_tx)
{
    USART_TypeDef* usart;

    if (!pdev)
        return;

    usart = stm32_usart_instance(pdev->cfg.uart_host);
    if (!usart)
        return;

    LL_USART_DisableDMAReq_TX(usart);

    if (dma_tx)
        COMPAT_IGNORE_RESULT(bus_dma_abort(dma_tx));
}
