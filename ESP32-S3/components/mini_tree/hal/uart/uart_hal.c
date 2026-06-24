#include "hal_uart.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"
#include <driver/uart.h>
#include "esp_err.h"
#include "dt_config_gen.h"

#define UART_DEVICE_COUNT                    DTC_GEN_COUNT_ESP32_UART1
#define UART_MAX_TRANSFER_BYTES                 512
#define UART_EVENT_QUEUE_SIZE                   20
#define UART_HOST_MAX                           3

static struct hal_uart_dev s_uart_dev[UART_DEVICE_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_uart_rx_buf[UART_DEVICE_COUNT][UART_MAX_TRANSFER_BYTES] COMPAT_ALIGNED(32);
static uint8_t s_uart_tx_buf[UART_DEVICE_COUNT][UART_MAX_TRANSFER_BYTES] COMPAT_ALIGNED(32);
static uint8_t s_host_mutex_storage[UART_HOST_MAX][OSAL_MUTEX_STORAGE_SIZE] COMPAT_ALIGNED(4) = {0};

static uart_word_length_t hal_uart_to_idf_data_bits(hal_uart_data_bits_t bits)
{
    switch (bits)
    {
    case HAL_UART_DATA_BITS_5: return UART_DATA_5_BITS;
    case HAL_UART_DATA_BITS_6: return UART_DATA_6_BITS;
    case HAL_UART_DATA_BITS_7: return UART_DATA_7_BITS;
    default:                   return UART_DATA_8_BITS;
    }
}

static uart_parity_t hal_uart_to_idf_parity(hal_uart_parity_t parity)
{
    switch (parity)
    {
    case HAL_UART_PARITY_EVEN: return UART_PARITY_EVEN;
    case HAL_UART_PARITY_ODD:  return UART_PARITY_ODD;
    default:                   return UART_PARITY_DISABLE;
    }
}

static uart_stop_bits_t hal_uart_to_idf_stop_bits(hal_uart_stop_bits_t stop_bits)
{
    switch (stop_bits)
    {
    case HAL_UART_STOP_BITS_1_5: return UART_STOP_BITS_1_5;
    case HAL_UART_STOP_BITS_2:   return UART_STOP_BITS_2;
    default:                     return UART_STOP_BITS_1;
    }
}

static int uart_dev_hw_idx(const struct hal_uart_dev* pdev)
{
    if (!pdev || pdev->pool_idx < 0)
        return VFS_ERR_INVAL;

    if (pdev->pool_idx >= UART_DEVICE_COUNT)
        return VFS_ERR_INVAL;
    return pdev->pool_idx;
}

static int uart_host_mutex_ensure(struct hal_uart_dev* pdev)
{
    if (!pdev)
        return VFS_ERR_INVAL;

    /* 互斥锁懒加载检查 */
    if (pdev->hal_mutex)
        return VFS_OK;

    int host_id = pdev->cfg.uart_host;
    if (host_id < 0 || host_id >= UART_HOST_MAX)
        return VFS_ERR_INVAL;

    struct osal_mutex* mtx = NULL;
    if (osal_mutex_create_static(&mtx, s_host_mutex_storage[host_id], sizeof(s_host_mutex_storage[host_id])) != 0)
        return VFS_ERR_NOMEM;

    pdev->hal_mutex = mtx;
    return VFS_OK;
}

static int uart_hw_init(const struct hal_uart_config_t* cfg)
{
    if (!cfg)
        return  VFS_ERR_INVAL;

    struct hal_uart_dev* pdev = container_of(cfg, struct hal_uart_dev, cfg);
    if (pdev->hw_inited)
        return VFS_OK;

    uart_config_t idf_cfg =
    {
        .baud_rate = (int)cfg->baud_rate,
        .data_bits = hal_uart_to_idf_data_bits(cfg->data_bits),
        .stop_bits = hal_uart_to_idf_stop_bits(cfg->stop_bits),
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .parity    = hal_uart_to_idf_parity(cfg->parity),
        .source_clk= UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config((uart_port_t)cfg->uart_host, &idf_cfg);
    if (err != ESP_OK)
        return VFS_ERR_IO;

    err = uart_set_pin(cfg->uart_host, HAL_PIN_NUM(cfg->tx_io), HAL_PIN_NUM(cfg->rx_io), UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
        return VFS_ERR_IO;

    err = uart_driver_install((uart_port_t)cfg->uart_host, UART_MAX_TRANSFER_BYTES, UART_MAX_TRANSFER_BYTES, UART_EVENT_QUEUE_SIZE, &pdev->uart_queue, ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK)
        return VFS_ERR_IO;

    pdev->hw_inited = true;
    return VFS_OK;
}

static int uart_hw_deinit(const struct hal_uart_config_t* cfg) 
{
    if (!cfg)
        return VFS_ERR_INVAL;

    esp_err_t err = uart_driver_delete((uart_port_t)cfg->uart_host);
    if (err != ESP_OK)
        return VFS_ERR_IO;

    struct hal_uart_dev* pdev = container_of(cfg, struct hal_uart_dev, cfg);
    pdev->hw_inited = false;
    return VFS_OK;
}

/* 填充发送缓冲区和清空接收缓冲区 */
static int uart_setup_trans(struct hal_uart_dev* pdev, size_t len, const uint8_t* tx)
{
    if (!pdev)
        return VFS_ERR_INVAL;

    size_t max_size = UART_MAX_TRANSFER_BYTES;
    int hw_idx      = uart_dev_hw_idx(pdev);
    if (len > max_size || hw_idx < 0)
        return VFS_ERR_INVAL;

    if (tx && len > 0)
        __builtin_memcpy(s_uart_tx_buf[hw_idx], tx, len);
    else if (len > 0)
        __builtin_memset(s_uart_tx_buf[hw_idx], 0, len);

    __builtin_memset(s_uart_rx_buf[hw_idx], 0, sizeof(s_uart_rx_buf[hw_idx]));
    return VFS_OK;
}

static int uart_read_impl(struct hal_uart_dev *pdev, uint8_t* data, size_t len)
{
    if (!pdev || !data || len == 0)
        return VFS_ERR_INVAL;

    int hw_idx = uart_dev_hw_idx(pdev);
    if (hw_idx < 0 || hw_idx >= UART_DEVICE_COUNT)
        return VFS_ERR_INVAL;

    if (uart_setup_trans(pdev, 0, NULL) != VFS_OK)
        return VFS_ERR_NOMEM;

    int read_len = uart_read_bytes(pdev->cfg.uart_host, s_uart_rx_buf[hw_idx], len, osal_timeout_to_ticks(10));
    if (read_len < 0)
        return VFS_ERR_IO;

    __builtin_memcpy(data, s_uart_rx_buf[hw_idx], read_len);
    return read_len;
}

static int uart_write_impl(struct hal_uart_dev* pdev, const uint8_t* data, size_t len)
{

    if (!pdev || !data || len == 0)
        return VFS_ERR_INVAL;

    int hw_idx = uart_dev_hw_idx(pdev);
    if (hw_idx < 0 || hw_idx >= UART_DEVICE_COUNT)
        return VFS_ERR_INVAL;

    if (uart_setup_trans(pdev, len, data) != VFS_OK)
        return VFS_ERR_NOMEM;

    int ret = uart_write_bytes(pdev->cfg.uart_host, s_uart_tx_buf[hw_idx], len);
    if (ret < 0)
        return VFS_ERR_IO;
    return ret;
}

static int uart_transmit_impl(struct hal_uart_dev* pdev, uint8_t* rx, uint8_t* tx, size_t rx_len, size_t tx_len)
{
    if (!pdev || !rx || !tx || rx_len == 0 || tx_len == 0)
        return VFS_ERR_INVAL;

    int hw_idx = uart_dev_hw_idx(pdev);
    if (hw_idx < 0 || hw_idx >= UART_DEVICE_COUNT)
        return VFS_ERR_INVAL;

    if (uart_setup_trans(pdev, tx_len, tx) != VFS_OK)
        return VFS_ERR_NOMEM;

    int ret = uart_write_bytes(pdev->cfg.uart_host, s_uart_tx_buf[hw_idx], tx_len);
    if (ret < 0)
        return VFS_ERR_IO;

    int read_len = uart_read_bytes(pdev->cfg.uart_host, s_uart_rx_buf[hw_idx], rx_len, osal_timeout_to_ticks(10));
    if (read_len < 0)
        return VFS_ERR_IO;

    __builtin_memcpy(rx, s_uart_rx_buf[hw_idx], read_len);
    return ret;
}

static int uart_bus_open(const struct hal_uart_config_t* cfg)
{
    struct hal_uart_dev* pdev = container_of(cfg, struct hal_uart_dev, cfg);
    int ret = uart_hw_init(cfg);
    if (ret == VFS_OK)
        pdev->hw_open = 1;
    return ret;
}

static int uart_bus_close(const struct hal_uart_config_t* cfg)
{
    struct hal_uart_dev* pdev = container_of(cfg, struct hal_uart_dev, cfg);
    int ret = uart_hw_deinit(cfg);
    if (ret == VFS_OK)
        pdev->hw_open = 0;
    return ret;
}

static int uart_bus_deinit(const struct hal_uart_config_t* cfg)
{
    struct hal_uart_dev* pdev = container_of(cfg, struct hal_uart_dev, cfg);
    int ret = uart_hw_deinit(cfg);
    pdev->hw_open = 0;
    return ret;
}

static const struct hal_uart_bus s_uart_bus =
{
    .open     = uart_bus_open,
    .close    = uart_bus_close,
    .read     = uart_read_impl,
    .write    = uart_write_impl,
    .transmit = uart_transmit_impl,
    .deinit   = uart_bus_deinit,
    ._impl    = NULL,
};

const struct hal_uart_bus* hal_uart_bus_get(void)
{
    return &s_uart_bus;
}

int hal_uart_xfer_begin(struct hal_uart_dev* pdev, uint32_t timeout_ms)
{
    if (!pdev)
        return VFS_ERR_INVAL;
    
    // 此调用懒加载函数，确保底层静态互斥锁实体被正常创建，避免 NULL 指针崩溃
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