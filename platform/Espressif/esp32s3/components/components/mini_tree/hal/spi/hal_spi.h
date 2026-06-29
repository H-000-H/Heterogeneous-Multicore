/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SPI HAL 层 — 硬件抽象接口 (ESP32-S3, Master + Slave)
 *
 * 与 STM32/CH32 hal_spi.h 结构对齐, 但支持 async 传输与 slave 模式。
 * st/ch 平台对应字段存在但 slave/async 返回 VFS_ERR_NOTSUPP。
 *
 * HAL 层不分配数据缓冲区。所有 tx/rx 指针由调用者提供。
 * 需要中间缓冲的场景在 VFS 层通过 buffer_pool 处理。
 */
#ifndef HAL_SPI_H
#define HAL_SPI_H

/*
 * HAL 层不分配数据缓冲区。所有 tx/rx 指针由调用者提供。
 * 需要中间缓冲的场景在 VFS 层通过 buffer_pool 处理。
 */
#include <stdint.h>
#include <stddef.h>
#include "hal_gpio.h"
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define HAL_SPI_BUS_ROLE_SLAVE  0
#define HAL_SPI_BUS_ROLE_MASTER 1

/* HAL 静态缓冲区尺寸上限 — DTS max-transfer-buffer 不得超过此值 */
#define HAL_SPI_MAX_TRANSFER_BYTES  2048

/* 每个 master device 最大并发 async transfer 数 */
#define HAL_SPI_MAX_ASYNC           4

/* forward decl */
struct hal_spi_dev;

/**
 * @brief SPI 传输完成 callback (ISR 上下文)
 *
 * @note 此 callback 在 DMA done 中断中调用, 严禁阻塞/睡眠/调 transfer。
 *       可读 rx_buffer, 但不可释放 trans 资源 (由 transfer_poll 回收)。
 */
typedef void (*hal_spi_callback_t)(struct hal_spi_dev* dev,
                                   const void* trans, void* userdata);

struct hal_spi_device_config
{
    int mode;
    int clock_speed_hz;
    hal_pin_t cs_pin;
    int queue_size;
};

struct hal_spi_bus_config
{
    int host_id;
    hal_pin_t mosi;
    hal_pin_t miso;
    hal_pin_t sclk;
    int max_transfer_sz;
    int dma_chan;
    int bus_role;
};

struct hal_spi_bus_host
{
    struct hal_spi_bus_config       cfg;
    int                             ref_count;
    int                             bus_ready;
    int                             hw_inited;
    struct hal_spi_device_config    active_cfg;
};

struct hal_spi_dev
{
    struct hal_spi_bus_host*        ctlr;
    struct hal_spi_device_config    cfg;
    int                             pool_idx;
    int                             hw_open;
};

typedef struct hal_spi_device_config spi_device_config;
typedef struct hal_spi_bus_config    spi_bus_config;
typedef struct hal_spi_bus_host      spi_controller;
typedef struct hal_spi_dev           spi_device;

int hal_spi_bus_host_init(int host_id, const struct hal_spi_bus_config* cfg);
int hal_spi_bus_host_deinit(int host_id);
int hal_spi_bus_host_get(int host_id, struct hal_spi_bus_host** out) COMPAT_WARN_UNUSED_RESULT;

void hal_spi_dev_init(struct hal_spi_dev* dev, int pool_idx,
                      struct hal_spi_bus_host* host,
                      const struct hal_spi_device_config* dev_cfg);

int hal_spi_dev_hw_open(struct hal_spi_dev* dev);
int hal_spi_dev_hw_close(struct hal_spi_dev* dev);

int spi_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
             size_t len, uint32_t timeout_ms);

/**
 * @brief 异步提交 SPI 传输 (master only, 非阻塞)
 *
 * 提交后立即返回。传输完成时在 ISR 上下文调用 cb。
 * 调用者随后必须调 hal_spi_transfer_poll 回收 trans 资源。
 *
 * @param dev      SPI device (master)
 * @param tx       发送缓冲 (NULL = 只收)
 * @param rx       接收缓冲 (NULL = 只发, 必须在 poll 前保持有效)
 * @param len      传输字节数
 * @param cb       传输完成 callback (ISR 上下文, 可 NULL)
 * @param userdata 传递给 cb 的用户数据
 * @return 成功返回 0, 失败返回 VFS_ERR_*
 */
int hal_spi_transfer_async(struct hal_spi_dev* dev,
                           const uint8_t* tx, uint8_t* rx,
                           size_t len, hal_spi_callback_t cb,
                           void* userdata);

/**
 * @brief 回收已完成的 async trans (阻塞等待)
 *
 * 必须在 transfer_async 提交后调用, 回收 trans 资源。
 * 若已注册 callback, 可在 callback 触发后调; 否则阻塞等待完成。
 *
 * @param dev        SPI device (master)
 * @param timeout_ms 超时
 * @return 成功返回 0, 超时返回 VFS_ERR_BUSY, 失败返回 VFS_ERR_*
 */
int hal_spi_transfer_poll(struct hal_spi_dev* dev, uint32_t timeout_ms);

static inline int hal_spi_transfer(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
                                   size_t len, uint32_t timeout_ms)
{
    return spi_sync(dev, tx, rx, len, timeout_ms);
}

int spi_slave_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
                   size_t len, uint32_t timeout_ms);

int spi_slave_queue_tx(struct hal_spi_dev* dev, const uint8_t* data, size_t len,
                       uint32_t timeout_ms);

int hal_spi_get_trans_result(struct hal_spi_dev* dev, uint8_t* rx_data, size_t rx_cap,
                             size_t* trans_len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_H */
