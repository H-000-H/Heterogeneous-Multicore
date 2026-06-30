/* SPDX-License-Identifier: Apache-2.0 */
/*
 * UART HAL — ESP32-S3 实现
 *
 * 设计: 硬件直投, DTSI 厂商宏值零翻译透传给 ESP-IDF uart driver。
 * - hal_uart_dev 嵌入 bus 层, HAL 无池管理无 vtable
 * - hw_open: uart_param_config + uart_set_pin + uart_driver_install (含事件队列)
 * - write/read 直接调 uart_write_bytes/uart_read_bytes, fast path 访问 dev->uart (缓存)
 * - uart_queue 为 FreeRTOS QueueHandle_t, 头中立用 void*, 本文件内部强转
 * ESP32 适配统一头: cfg.uart 承载 uart_port_t, cfg.tx/rx 为 hal_uart_pin_cfg
 *                   (port=0, clk_periph=0, af=0, pin=SoC GPIO 编号)。
 * DMA 不支持 (ESP32 无 STM32/WCH 风格 DMA API), 返回 VFS_ERR_NOTSUPP。
 */
#include "hal_uart.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <driver/uart.h>
#include "esp_err.h"

#include "dt_config_gen.h"

#define UART_EVENT_QUEUE_SIZE   20

#ifndef DTC_GEN_ESP32_UART_HOST_MAX
#define DTC_GEN_ESP32_UART_HOST_MAX  3
#endif
#ifndef DTC_GEN_ESP32_UART_MAX_XFER
#define DTC_GEN_ESP32_UART_MAX_XFER  512
#endif

#define UART_HOST_MAX           DTC_GEN_ESP32_UART_HOST_MAX
#define UART_MAX_TRANSFER_BYTES DTC_GEN_ESP32_UART_MAX_XFER

/*============================================================================*/
/*                              Device 管理 API                               */
/*============================================================================*/
/**
 * @brief UART Device 对象初始化: 清零 + 拷贝配置 + 缓存 uart + 标记 UNINIT
 * @param dev      Device 对象指针
 * @param pool_idx 设备在 bus 层池中的索引
 * @param cfg      UART 配置 (DTSI ESP-IDF 枚举值)
 */
void hal_uart_dev_init(struct hal_uart_dev* dev, int pool_idx,
                       const struct hal_uart_config* cfg)
{
    __builtin_memset(dev, 0, sizeof(*dev));
    dev->cfg      = *cfg;
    dev->uart     = cfg->uart;   /* fast path 缓存 */
    dev->pool_idx = pool_idx;
    dev->status   = UART_STATE_UNINIT;
}

/**
 * @brief 打开 UART 硬件: uart_param_config + uart_set_pin + uart_driver_install (含事件队列)
 * @param dev Device 对象指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 已初始化直接返回 VFS_OK, ESP-IDF 失败返回 VFS_ERR_IO
 */
int hal_uart_dev_hw_open(struct hal_uart_dev* dev)
{
    uart_config_t idf_cfg;
    esp_err_t     err;

    if (!dev)
        return VFS_ERR_INVAL;
    if (dev->hw_inited)
        return VFS_OK;

    /* 硬件直投: DTSI 提供的 ESP-IDF 枚举值零翻译灌入 uart_config_t */
    __builtin_memset(&idf_cfg, 0, sizeof(idf_cfg));
    idf_cfg.baud_rate  = (int)dev->cfg.baud_rate;
    idf_cfg.data_bits  = (uart_word_length_t)dev->cfg.data_width;
    idf_cfg.stop_bits  = (uart_stop_bits_t)dev->cfg.stop_bits;
    idf_cfg.parity     = (uart_parity_t)dev->cfg.parity;
    idf_cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    idf_cfg.source_clk = UART_SCLK_DEFAULT;

    err = uart_param_config((uart_port_t)dev->uart, &idf_cfg);
    if (err != ESP_OK)
        return VFS_ERR_IO;

    err = uart_set_pin((uart_port_t)dev->uart, dev->cfg.tx.pin, dev->cfg.rx.pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
        return VFS_ERR_IO;

    /* uart_queue 头中立用 void*, 此处强转 QueueHandle_t* 供 ESP-IDF 回填 */
    err = uart_driver_install((uart_port_t)dev->uart,
                              UART_MAX_TRANSFER_BYTES, UART_MAX_TRANSFER_BYTES,
                              UART_EVENT_QUEUE_SIZE,
                              (QueueHandle_t*)&dev->uart_queue, ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK)
        return VFS_ERR_IO;

    dev->hw_inited = true;
    dev->hw_open   = 1;
    dev->status    = UART_STATE_READY;
    return VFS_OK;
}

/**
 * @brief 关闭 UART 硬件: uart_driver_delete 释放驱动 + 标记未初始化
 * @param dev Device 对象指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, ESP-IDF 失败返回 VFS_ERR_IO
 */
int hal_uart_dev_hw_close(struct hal_uart_dev* dev)
{
    esp_err_t err;

    if (!dev)
        return VFS_ERR_INVAL;

    err = uart_driver_delete((uart_port_t)dev->uart);
    if (err != ESP_OK)
        return VFS_ERR_IO;

    dev->hw_inited = false;
    dev->hw_open   = 0;
    dev->uart_queue = NULL;
    dev->status    = UART_STATE_UNINIT;
    return VFS_OK;
}

/*============================================================================*/
/*                              同步传输                                       */
/*============================================================================*/
/**
 * @brief UART 同步写: 直调 uart_write_bytes 发送 (调用者缓冲直传, 无内部拷贝)
 * @param dev  Device 对象指针
 * @param data 待发送数据
 * @param len  字节数
 * @return 成功返回写入字节数, 参数非法返回 VFS_ERR_INVAL, ESP-IDF 失败返回 VFS_ERR_IO
 */
int hal_uart_write(struct hal_uart_dev* dev, const uint8_t* data, size_t len)
{
    int ret;

    if (!dev || !data || len == 0)
        return VFS_ERR_INVAL;

    dev->status = UART_STATE_BUSY;
    ret = uart_write_bytes((uart_port_t)dev->uart, data, len);
    if (ret < 0)
    {
        dev->status = UART_STATE_ERROR;
        return VFS_ERR_IO;
    }
    dev->status = UART_STATE_READY;
    return ret;
}

/**
 * @brief UART 同步读: 直调 uart_read_bytes 从 ESP-IDF ring buffer 读取 (直读调用者缓冲)
 * @param dev  Device 对象指针
 * @param data 接收缓冲区
 * @param len  字节数
 * @return 成功返回读到的字节数, 参数非法返回 VFS_ERR_INVAL, ESP-IDF 失败返回 VFS_ERR_IO
 */
int hal_uart_read(struct hal_uart_dev* dev, uint8_t* data, size_t len)
{
    int read_len;

    if (!dev || !data || len == 0)
        return VFS_ERR_INVAL;

    dev->status = UART_STATE_BUSY;
    read_len = uart_read_bytes((uart_port_t)dev->uart, data, len,
                               osal_timeout_to_ticks(10));
    if (read_len < 0)
    {
        dev->status = UART_STATE_ERROR;
        return VFS_ERR_IO;
    }
    dev->status = UART_STATE_READY;
    return read_len;
}

/*============================================================================*/
/*                              强制停止 API                                   */
/*============================================================================*/
/**
 * @brief 强制停止所有 ESP32 UART (panic/reboot 路径, 逐端口调 uart_driver_delete)
 * @return 成功返回 VFS_OK
 */
int hal_uart_force_stop(void)
{
    for (int i = 0; i < UART_HOST_MAX; i++)
    {
        uart_driver_delete((uart_port_t)i);
    }
    return VFS_OK;
}

/*============================================================================*/
/*                              DMA 传输 (ESP32 不支持, 返回 NOTSUPP)          */
/*============================================================================*/
/**
 * @brief UART DMA 写 (ESP32 不支持, 返回 VFS_ERR_NOTSUPP)
 * @note  统一 HAL 头要求声明此 API, ESP32 无 STM32/WCH 风格 DMA, stub 返回 NOTSUPP
 */
int hal_uart_write_dma(struct hal_uart_dev* pdev,
                        struct bus_dma_chan* dma_tx,
                        const uint8_t* data, size_t len,
                        uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(pdev);
    COMPAT_IGNORE_RESULT(dma_tx);
    COMPAT_IGNORE_RESULT(data);
    COMPAT_IGNORE_RESULT(len);
    COMPAT_IGNORE_RESULT(timeout_ms);
    return VFS_ERR_NOTSUPP;
}

/**
 * @brief UART DMA 中止 (ESP32 不支持, 空实现)
 * @note  统一 HAL 头要求声明此 API, ESP32 无 DMA 可中止, 空实现
 */
void hal_uart_dma_abort(struct hal_uart_dev* pdev, struct bus_dma_chan* dma_tx)
{
    COMPAT_IGNORE_RESULT(pdev);
    COMPAT_IGNORE_RESULT(dma_tx);
}
