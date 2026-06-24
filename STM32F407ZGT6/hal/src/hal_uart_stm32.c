/*
 * STM32F407 UART HAL
 *
 * - 时钟/GPIO：由 CubeMX MX_UART4_Init() 在 main pre_execution 钩子完成
 * - 参数/传输：LL_USART_* 库函数，不直接写寄存器，不配置 RCC
 */
#include "hal_uart.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx.h"

#include <string.h>

#define UART_HOST_MAX        4
#define UART_READ_TIMEOUT_MS 10U

static uint8_t s_host_mutex_storage[UART_HOST_MAX][OSAL_MUTEX_STORAGE_SIZE];

static USART_TypeDef* stm32_usart_instance(int host_id)
{
    switch (host_id)
    {
    case 0:
        return UART4;
    default:
        return NULL;
    }
}

static struct hal_uart_dev* stm32_uart_dev_from_cfg(const struct hal_uart_config_t* cfg)
{
    if (!cfg)
        return NULL;
    return container_of(cfg, struct hal_uart_dev, cfg);
}

static uint32_t stm32_uart_data_width(hal_uart_data_bits_t bits)
{
    (void)bits;
    return LL_USART_DATAWIDTH_8B;
}

static uint32_t stm32_uart_parity(hal_uart_parity_t parity)
{
    switch (parity)
    {
    case HAL_UART_PARITY_EVEN:
        return LL_USART_PARITY_EVEN;
    case HAL_UART_PARITY_ODD:
        return LL_USART_PARITY_ODD;
    default:
        return LL_USART_PARITY_NONE;
    }
}

static uint32_t stm32_uart_stop_bits(hal_uart_stop_bits_t stop_bits)
{
    switch (stop_bits)
    {
    case HAL_UART_STOP_BITS_2:
        return LL_USART_STOPBITS_2;
    case HAL_UART_STOP_BITS_1_5:
        return LL_USART_STOPBITS_1_5;
    default:
        return LL_USART_STOPBITS_1;
    }
}

static int stm32_uart_apply_config(USART_TypeDef* usart, const struct hal_uart_config_t* cfg)
{
    LL_USART_InitTypeDef init = {0};

    if (!usart || !cfg || cfg->baud_rate == 0U)
        return VFS_ERR_INVAL;

    LL_USART_Disable(usart);
    init.BaudRate            = cfg->baud_rate;
    init.DataWidth           = stm32_uart_data_width(cfg->data_bits);
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

static int uart_host_mutex_ensure(struct hal_uart_dev* pdev)
{
    int host_id;

    if (!pdev)
        return VFS_ERR_INVAL;

    if (pdev->hal_mutex)
        return VFS_OK;

    host_id = pdev->cfg.uart_host;
    if (host_id < 0 || host_id >= UART_HOST_MAX)
        return VFS_ERR_INVAL;

    struct osal_mutex* mtx = NULL;
    if (osal_mutex_create_static(&mtx, s_host_mutex_storage[host_id],
                                 sizeof(s_host_mutex_storage[host_id])) != 0)
        return VFS_ERR_NOMEM;

    pdev->hal_mutex = mtx;
    return VFS_OK;
}

static int stm32_uart_write_poll(USART_TypeDef* usart, const uint8_t* data, size_t len)
{
    size_t i;

    if (!usart || !data || len == 0U)
        return VFS_ERR_INVAL;

    for (i = 0; i < len; i++)
    {
        while (!LL_USART_IsActiveFlag_TXE(usart))
            ;
        LL_USART_TransmitData8(usart, data[i]);
    }

    while (!LL_USART_IsActiveFlag_TC(usart))
        ;
    return (int)len;
}

static int stm32_uart_read_poll(USART_TypeDef* usart, uint8_t* data, size_t len)
{
    size_t i;
    uint32_t timeout_ms = UART_READ_TIMEOUT_MS;

    if (!usart || !data || len == 0U)
        return VFS_ERR_INVAL;

    for (i = 0; i < len; i++)
    {
        uint32_t waited = 0U;
        while (!LL_USART_IsActiveFlag_RXNE(usart))
        {
            if (waited >= timeout_ms)
                return (int)i;
            osal_delay_ms(1U);
            waited++;
        }
        data[i] = LL_USART_ReceiveData8(usart);
    }

    return (int)len;
}

static int stm32_uart_open(const struct hal_uart_config_t* cfg)
{
    struct hal_uart_dev* pdev;
    USART_TypeDef*       usart;

    pdev = stm32_uart_dev_from_cfg(cfg);
    if (!pdev)
        return VFS_ERR_INVAL;

    usart = stm32_usart_instance(cfg->uart_host);
    if (!usart)
        return VFS_ERR_NODEV;

    if (stm32_uart_apply_config(usart, cfg) != VFS_OK)
        return VFS_ERR_IO;

    pdev->hw_inited = true;
    pdev->hw_open   = 1;
    pdev->status    = UART_STATE_READY;
    return VFS_OK;
}

static int stm32_uart_close(const struct hal_uart_config_t* cfg)
{
    struct hal_uart_dev* pdev;
    USART_TypeDef*       usart;

    pdev = stm32_uart_dev_from_cfg(cfg);
    if (!pdev)
        return VFS_ERR_INVAL;

    usart = stm32_usart_instance(cfg->uart_host);
    if (usart)
        LL_USART_Disable(usart);

    pdev->hw_open   = 0;
    pdev->hw_inited = false;
    pdev->status    = UART_STATE_UNINIT;
    return VFS_OK;
}

static int stm32_uart_deinit(const struct hal_uart_config_t* cfg)
{
    return stm32_uart_close(cfg);
}

static int stm32_uart_read(struct hal_uart_dev* pdev, uint8_t* data, size_t len)
{
    USART_TypeDef* usart;

    if (!pdev || !data || len == 0U || !pdev->hw_open)
        return VFS_ERR_INVAL;

    usart = stm32_usart_instance(pdev->cfg.uart_host);
    if (!usart)
        return VFS_ERR_NODEV;

    return stm32_uart_read_poll(usart, data, len);
}

static int stm32_uart_write(struct hal_uart_dev* pdev, const uint8_t* data, size_t len)
{
    USART_TypeDef* usart;
    int            ret;

    if (!pdev || !data || len == 0U || !pdev->hw_open)
        return VFS_ERR_INVAL;

    usart = stm32_usart_instance(pdev->cfg.uart_host);
    if (!usart)
        return VFS_ERR_NODEV;

    ret = stm32_uart_write_poll(usart, data, len);
    return ret < 0 ? ret : ret;
}

static int stm32_uart_transmit(struct hal_uart_dev* pdev, uint8_t* rx, uint8_t* tx, size_t rx_len,
                               size_t tx_len)
{
    int ret;

    if (!pdev || !rx || !tx || rx_len == 0U || tx_len == 0U)
        return VFS_ERR_INVAL;

    ret = stm32_uart_write(pdev, tx, tx_len);
    if (ret < 0)
        return ret;

    return stm32_uart_read(pdev, rx, rx_len);
}

static const struct hal_uart_bus s_uart_bus = {
    .open     = stm32_uart_open,
    .close    = stm32_uart_close,
    .read     = stm32_uart_read,
    .write    = stm32_uart_write,
    .transmit = stm32_uart_transmit,
    .deinit   = stm32_uart_deinit,
};

const struct hal_uart_bus* hal_uart_bus_get(void)
{
    return &s_uart_bus;
}

int hal_uart_xfer_begin(struct hal_uart_dev* pdev, uint32_t timeout_ms)
{
    if (!pdev)
        return VFS_ERR_INVAL;

    if (uart_host_mutex_ensure(pdev) != VFS_OK)
        return VFS_ERR_NOMEM;

    if (osal_mutex_lock(pdev->hal_mutex, timeout_ms) != 0)
        return VFS_ERR_BUSY;

    pdev->status = UART_STATE_BUSY;
    return VFS_OK;
}

int hal_uart_xfer_end(struct hal_uart_dev* pdev)
{
    if (!pdev)
        return VFS_ERR_INVAL;

    pdev->status = UART_STATE_READY;
    return osal_mutex_unlock(pdev->hal_mutex) == 0 ? VFS_OK : VFS_ERR_IO;
}

int hal_uart_force_stop(void)
{
    return VFS_OK;
}
