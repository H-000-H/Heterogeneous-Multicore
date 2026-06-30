/* SPDX-License-Identifier: Apache-2.0 */
/*
 * SPI HAL — ESP32-S3 实现 (Master + Slave)
 *
 * 适配统一 hal_spi.h 结构体与 API, 调用 ESP-IDF spi_master/spi_slave driver。
 * 支持 async 传输 (事务队列), callback 在 ISR 上下文调用。
 * 平台特性: Master 用 spi_device_handle_t 事务队列; Slave 回调在 SPI_INTR ISR 触发;
 *           DMA 用 ESP32-S3 GDMA, 自动管理 cache 同步。
 * ESP32 适配统一头: cfg.spi 承载 spi_host_device_t, cfg.mosi/miso/sclk 为 hal_spi_pin_cfg
 *                   (port=0, clk_periph=0, af=0, pin=SoC GPIO 编号)。
 */
#define HAL_SPI_INTERNAL
#include "hal_spi.h"
#include "VFS.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "compiler_compat.h"
#include "system_log.h"

#include "driver/spi_slave.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "hal/gpio_types.h"
#include "esp_err.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

#define SPI_SLAVE_DEVICE_COUNT        DTC_GEN_COUNT_HETEROGENEOUS_FFT_SPI_SLAVE
#define SPI_MASTER_DEVICE_COUNT       DTC_GEN_COUNT_HETEROGENEOUS_SPI_MASTER_CLIENT
#define SPI_DEVICE_COUNT              (SPI_SLAVE_DEVICE_COUNT + SPI_MASTER_DEVICE_COUNT)
#define SPI_SLAVE_MAX_TRANSFER_BYTES  HAL_SPI_MAX_TRANSFER_BYTES

                                                            /*硬件上下文结构*/
/*===========================================================================================================================================================*/
struct hal_spi_hw
{
    union
    {
        struct
        {
            spi_slave_transaction_t trans;
            atomic_bool             trans_queued;
        } slave;
        struct
        {
            spi_device_handle_t handle;
        } master;
    } u;
    int is_master;
};

static struct hal_spi_hw s_spi_hw[SPI_DEVICE_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_spi_rx_buf[SPI_DEVICE_COUNT][SPI_SLAVE_MAX_TRANSFER_BYTES] COMPAT_ALIGNED(32) = {0};
static uint8_t s_spi_tx_buf[SPI_DEVICE_COUNT][SPI_SLAVE_MAX_TRANSFER_BYTES] COMPAT_ALIGNED(32) = {0};
static uint8_t s_spi_dummy_rx_buf[SPI_DEVICE_COUNT][SPI_SLAVE_MAX_TRANSFER_BYTES] COMPAT_ALIGNED(32) = {0};

/* async trans 池: 每个 hw_idx 独立 HAL_SPI_MAX_ASYNC 个 slot */
struct hal_spi_trans {
    spi_transaction_t   idf_trans;
    hal_spi_callback_t  cb;
    void*               userdata;
    struct hal_spi_dev* dev;
    uint8_t             in_use;
};
static struct hal_spi_trans s_spi_trans_pool[SPI_DEVICE_COUNT][HAL_SPI_MAX_ASYNC] COMPAT_ALIGNED(4);

static const char* const kTag = "hal_spi";
/*===========================================================================================================================================================*/

/* 前向声明: 以下函数定义在文件后部, 但被 sync 路径提前调用 */
static int spi_controller_apply_dev_cfg(struct hal_spi_dev* dev);
static int spi_controller_xfer(struct hal_spi_bus_host* host, struct hal_spi_hw* hw,
                                const uint8_t* tx, uint8_t* rx, size_t len);
static int spi_slave_bus_reconfigure(struct hal_spi_bus_host* host,
                                      const struct hal_spi_device_config* cfg);
static int spi_slave_controller_xfer(struct hal_spi_dev* dev, struct hal_spi_hw* hw,
                                     const uint8_t* tx, uint8_t* rx,
                                     size_t len, uint32_t timeout_ms);
static int spi_slave_queue_tx_internal(struct hal_spi_dev* dev, struct hal_spi_hw* hw,
                                        const uint8_t* data, size_t len,
                                        uint32_t timeout_ms);


                                                            /*HW slot 索引工具*/
/*===========================================================================================================================================================*/
/**
 * @brief 由 dev 的 pool_idx 与角色 (master/slave) 计算 s_spi_hw 数组的全局索引
 * @param dev Device 对象指针
 * @return master: SPI_SLAVE_DEVICE_COUNT + pool_idx; slave: pool_idx; 非法返回 -1
 */
static int spi_dev_hw_idx(const struct hal_spi_dev* dev)
{
    if (!dev || !dev->ctlr || dev->pool_idx < 0)
        return -1;

    if (dev->ctlr->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
    {
        if (dev->pool_idx >= SPI_MASTER_DEVICE_COUNT)
            return -1;
        return SPI_SLAVE_DEVICE_COUNT + dev->pool_idx;
    }

    if (dev->pool_idx >= SPI_SLAVE_DEVICE_COUNT)
        return -1;
    return dev->pool_idx;
}

/**
 * @brief 取 dev 对应的 hal_spi_hw 硬件上下文指针
 * @param dev Device 对象指针
 * @return hal_spi_hw 指针, hw_idx 非法返回 NULL
 */
struct hal_spi_hw* spi_dev_hw(const struct hal_spi_dev* dev)
{
    int hw_idx = spi_dev_hw_idx(dev);
    if (hw_idx >= 0 && hw_idx < SPI_DEVICE_COUNT)
        return &s_spi_hw[hw_idx];
    return NULL;
}

/**
 * @brief 初始化某个 pool_idx 对应的 s_spi_hw slot (清零 + 标记角色 + 复位 trans_queued)
 * @param pool_idx  设备池索引
 * @param is_master 1=master, 0=slave
 */
void spi_dev_hw_slot_init(int pool_idx, int is_master)
{
    int hw_idx;

    if (pool_idx < 0)
        return;

    hw_idx = is_master ? SPI_SLAVE_DEVICE_COUNT + pool_idx : pool_idx;
    if (hw_idx < 0 || hw_idx >= SPI_DEVICE_COUNT)
        return;

    __builtin_memset(&s_spi_hw[hw_idx], 0, sizeof(s_spi_hw[hw_idx]));
    s_spi_hw[hw_idx].is_master = is_master;
    if (!is_master)
        atomic_store(&s_spi_hw[hw_idx].u.slave.trans_queued, false);
}

/**
 * @brief 取 host 允许的最大传输字节数 (优先用 DTS 配置, 否则回退 SPI_SLAVE_MAX_TRANSFER_BYTES)
 * @param host Host 对象指针
 * @return 最大传输字节数
 */
static size_t spi_host_max_transfer_bytes(const struct hal_spi_bus_host* host)
{
    if (!host)
        return SPI_SLAVE_MAX_TRANSFER_BYTES;
    if (host->cfg.max_transfer_sz > 0)
        return (size_t)host->cfg.max_transfer_sz;
    return SPI_SLAVE_MAX_TRANSFER_BYTES;
}

/**
 * @brief 取 host 的 ESP-IDF spi_host_device_t (强转 cfg.spi)
 * @param host Host 对象指针
 * @return spi_host_device_t 枚举值
 */
static spi_host_device_t spi_host_id(const struct hal_spi_bus_host* host)
{
    return (spi_host_device_t)host->cfg.spi;
}
/*===========================================================================================================================================================*/

                                                            /*核心传输层 (master/slave sync)*/
/*===========================================================================================================================================================*/
/**
 * @brief 取 controller (host) 允许的最大传输字节数 (优先用 DTS 配置, 否则回退 2048)
 * @param ctlr Host 对象指针
 * @return 最大传输字节数, ctlr 为空返回 0
 */
static size_t spi_ctlr_max_transfer_bytes(const struct hal_spi_bus_host* ctlr)
{
    if (!ctlr)
        return 0;
    if (ctlr->cfg.max_transfer_sz > 0)
        return (size_t)ctlr->cfg.max_transfer_sz;
    return 2048U;
}

/**
 * @brief SPI Master 同步传输: 应用 device 配置 → 取 hw → 转 spi_controller_xfer 执行
 * @param dev        Device 对象指针 (必须已 hw_open 且 host 为 master)
 * @param tx         发送缓冲区 (可为 NULL, 内部用 dummy)
 * @param rx         接收缓冲区 (可为 NULL, 内部用 dummy)
 * @param len        传输字节数
 * @param timeout_ms 超时 (ms, 当前实现未使用)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 取 hw 失败或传输失败返回 VFS_ERR_IO
 */
int spi_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
             size_t len, uint32_t timeout_ms)
{
    struct hal_spi_hw* hw;
    int                ret;
    int                xfer_ret;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!dev || !dev->ctlr || !dev->hw_open || len == 0)
        return VFS_ERR_INVAL;

    if (dev->ctlr->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    if (len > spi_ctlr_max_transfer_bytes(dev->ctlr))
        return VFS_ERR_INVAL;

    ret = spi_controller_apply_dev_cfg(dev);
    if (ret != VFS_OK)
        return ret;

    hw = spi_dev_hw(dev);
    if (!hw)
        return VFS_ERR_IO;

    xfer_ret = spi_controller_xfer(dev->ctlr, hw, tx, rx, len);
    if (xfer_ret < 0)
        return VFS_ERR_IO;

    return VFS_OK;
}

/**
 * @brief SPI Slave 同步传输: 重配 slave 总线 → 取 hw → 转 spi_slave_controller_xfer 执行
 * @param dev        Device 对象指针 (必须已 hw_open 且 host 为 slave)
 * @param tx         发送缓冲区 (可与 rx 同时为空)
 * @param rx         接收缓冲区 (可与 tx 同时为空)
 * @param len        传输字节数
 * @param timeout_ms 超时 (ms)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 取 hw 失败或传输失败返回 VFS_ERR_IO
 */
int spi_slave_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
                   size_t len, uint32_t timeout_ms)
{
    struct hal_spi_hw* hw;
    int                ret;
    int                xfer_ret;

    if (!dev || !dev->ctlr || !dev->hw_open || len == 0)
        return VFS_ERR_INVAL;

    if (dev->ctlr->cfg.bus_role != HAL_SPI_BUS_ROLE_SLAVE)
        return VFS_ERR_INVAL;

    if (!tx && !rx)
        return VFS_ERR_INVAL;

    if (len > spi_ctlr_max_transfer_bytes(dev->ctlr))
        return VFS_ERR_INVAL;

    ret = spi_slave_bus_reconfigure(dev->ctlr, &dev->cfg);
    if (ret != VFS_OK)
        return ret;

    hw = spi_dev_hw(dev);
    if (!hw)
        return VFS_ERR_IO;

    xfer_ret = spi_slave_controller_xfer(dev, hw, tx, rx, len, timeout_ms);
    if (xfer_ret < 0)
        return VFS_ERR_IO;

    return VFS_OK;
}

/**
 * @brief SPI Slave 异步队列发送: 重配 slave 总线 → 取 hw → 转 spi_slave_queue_tx_internal 入队
 * @param dev        Device 对象指针 (必须已 hw_open 且 host 为 slave)
 * @param data       待发送数据
 * @param len        字节数
 * @param timeout_ms 入队等待超时 (ms)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 取 hw 失败或入队失败返回 VFS_ERR_IO
 */
int spi_slave_queue_tx(struct hal_spi_dev* dev, const uint8_t* data, size_t len,
                       uint32_t timeout_ms)
{
    struct hal_spi_hw* hw;
    int                ret;
    int                xfer_ret;

    if (!dev || !dev->ctlr || !dev->hw_open || !data || len == 0)
        return VFS_ERR_INVAL;

    if (dev->ctlr->cfg.bus_role != HAL_SPI_BUS_ROLE_SLAVE)
        return VFS_ERR_INVAL;

    if (len > spi_ctlr_max_transfer_bytes(dev->ctlr))
        return VFS_ERR_INVAL;

    ret = spi_slave_bus_reconfigure(dev->ctlr, &dev->cfg);
    if (ret != VFS_OK)
        return ret;

    hw = spi_dev_hw(dev);
    if (!hw)
        return VFS_ERR_IO;

    xfer_ret = spi_slave_queue_tx_internal(dev, hw, data, len, timeout_ms);
    if (xfer_ret < 0)
        return VFS_ERR_IO;

    return VFS_OK;
}
/*===========================================================================================================================================================*/

                                                            /*Slave 平台实现*/
/*===========================================================================================================================================================*/
/**
 * @brief SPI Slave 硬件初始化: 调 spi_slave_initialize 注册 slave 总线 (含引脚/CS/DMA 通道)
 * @param host     Host 对象指针
 * @param dev_cfg  设备配置 (cs_pin/mode/queue_size)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, ESP-IDF 失败返回 VFS_ERR_IO
 */
static int spi_slave_hw_init(struct hal_spi_bus_host* host,
                             const struct hal_spi_device_config* dev_cfg)
{
    const struct hal_spi_bus_config* bus_cfg;

    if (!host || !dev_cfg)
        return VFS_ERR_INVAL;
    if (host->hw_inited)
        return VFS_OK;

    bus_cfg = &host->cfg;

    spi_bus_config_t idf_bus_cfg =
    {
        .mosi_io_num = bus_cfg->mosi.pin,
        .miso_io_num = bus_cfg->miso.pin,
        .sclk_io_num = bus_cfg->sclk.pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = bus_cfg->max_transfer_sz > 0 ?
                           bus_cfg->max_transfer_sz : (int)SPI_SLAVE_MAX_TRANSFER_BYTES,
    };

    spi_slave_interface_config_t slave_cfg =
    {
        .spics_io_num = dev_cfg->cs_pin,
        .mode = (uint8_t)dev_cfg->mode,
        .queue_size = dev_cfg->queue_size > 0 ? dev_cfg->queue_size : 4,
        .post_setup_cb = NULL,
        .post_trans_cb = NULL,
    };

    spi_dma_chan_t dma_chan = bus_cfg->dma_chan >= 0 ?
                              (spi_dma_chan_t)bus_cfg->dma_chan : SPI_DMA_CH_AUTO;

    esp_err_t err = spi_slave_initialize(spi_host_id(host), &idf_bus_cfg, &slave_cfg, dma_chan);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        SYS_LOGE(kTag, "spi_slave_initialize failed: %d", (int)err);
        return VFS_ERR_IO;
    }

    host->hw_inited = 1;
    host->active_cfg = *dev_cfg;
    return VFS_OK;
}

/**
 * @brief SPI Slave 硬件反初始化: 调 spi_slave_disable + spi_slave_free 释放 slave 总线
 * @param host Host 对象指针
 * @return 成功返回 VFS_OK, host 为空或未初始化直接返回 VFS_OK, spi_slave_free 失败返回 VFS_ERR_IO
 */
static int spi_slave_hw_deinit(struct hal_spi_bus_host* host)
{
    spi_host_device_t idf_host;

    if (!host || !host->hw_inited)
        return VFS_OK;

    idf_host = spi_host_id(host);

    COMPAT_IGNORE_RESULT(spi_slave_disable(idf_host));
    if (spi_slave_free(idf_host) != ESP_OK)
    {
        SYS_LOGE(kTag, "spi_slave_free failed");
        return VFS_ERR_IO;
    }

    host->hw_inited = 0;
    __builtin_memset(&host->active_cfg, 0, sizeof(host->active_cfg));
    return VFS_OK;
}

/**
 * @brief 准备 slave 传输事务: 拷贝 tx 到内部缓冲 + 填充 spi_slave_transaction_t
 * @param dev    Device 对象指针
 * @param hw     硬件上下文指针
 * @param len    传输字节数
 * @param tx     发送数据 (可为 NULL, 内部填 0)
 * @param rx_buf 接收缓冲指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL
 */
static int spi_slave_setup_trans(struct hal_spi_dev* dev, struct hal_spi_hw* hw,
                                 size_t len, const uint8_t* tx, uint8_t* rx_buf)
{
    size_t max_len = spi_host_max_transfer_bytes(dev->ctlr);
    int    hw_idx  = spi_dev_hw_idx(dev);

    if (len > max_len || hw_idx < 0)
        return VFS_ERR_INVAL;

    if (tx)
        __builtin_memcpy(s_spi_tx_buf[hw_idx], tx, len);
    else
        __builtin_memset(s_spi_tx_buf[hw_idx], 0, len);

    __builtin_memset(&hw->u.slave.trans, 0, sizeof(hw->u.slave.trans));
    hw->u.slave.trans.length = len * 8U;
    hw->u.slave.trans.tx_buffer = s_spi_tx_buf[hw_idx];
    hw->u.slave.trans.rx_buffer = rx_buf;
    return VFS_OK;
}

/**
 * @brief Slave 总线重配检测: master 角色 OK 直接返回; slave 角色比较 CS/mode/queue_size, 一致则跳过, 否则返回 IO 错误 (ESP32 slave 不支持运行时切换)
 * @param host     Host 对象指针
 * @param dev_cfg  目标 device 配置
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, hw 未初始化返回 VFS_ERR_IO, 配置不一致返回 VFS_ERR_IO
 */
int spi_slave_bus_reconfigure(struct hal_spi_bus_host* host,
                              const struct hal_spi_device_config* dev_cfg)
{
    if (!host || !dev_cfg || !host->bus_ready)
        return VFS_ERR_INVAL;

    if (!host->hw_inited)
        return VFS_ERR_IO;

    if (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
        return VFS_OK;

    if (host->active_cfg.cs_pin == dev_cfg->cs_pin &&
        host->active_cfg.mode == dev_cfg->mode &&
        host->active_cfg.queue_size == dev_cfg->queue_size)
        return VFS_OK;

    return VFS_ERR_IO;
}

/**
 * @brief SPI Slave 同步执行一次传输: setup_trans → spi_slave_transmit 阻塞等待完成
 * @param dev        Device 对象指针
 * @param hw         硬件上下文指针
 * @param tx         发送数据 (可为 NULL)
 * @param rx         接收缓冲 (为 NULL 时用 dummy)
 * @param len        字节数
 * @param timeout_ms 阻塞超时 (ms)
 * @return 成功返回传输字节数, 参数非法返回 VFS_ERR_INVAL, 事务已排队返回 VFS_ERR_BUSY, ESP-IDF 失败返回 VFS_ERR_IO
 */
int spi_slave_controller_xfer(struct hal_spi_dev* dev, struct hal_spi_hw* hw,
                              const uint8_t* tx, uint8_t* rx, size_t len,
                              uint32_t timeout_ms)
{
    int    hw_idx;
    int    ret;
    uint8_t* rx_work;

    if (!dev || !dev->ctlr || !hw || !dev->ctlr->hw_inited || len == 0)
        return VFS_ERR_INVAL;

    if (atomic_load_explicit(&hw->u.slave.trans_queued, memory_order_acquire))
        return VFS_ERR_BUSY;

    hw_idx = spi_dev_hw_idx(dev);
    if (hw_idx < 0)
        return VFS_ERR_INVAL;

    rx_work = rx ? rx : s_spi_dummy_rx_buf[hw_idx];

    ret = spi_slave_setup_trans(dev, hw, len, tx, rx_work);
    if (ret != VFS_OK)
        return ret;

    if (spi_slave_transmit(spi_host_id(dev->ctlr), &hw->u.slave.trans,
                           osal_timeout_to_ticks(timeout_ms)) != ESP_OK)
        return VFS_ERR_IO;

    return (int)len;
}

/**
 * @brief SPI Slave 异步入队内部实现: setup_trans → spi_slave_queue_trans 非阻塞入队 + 置 trans_queued
 * @param dev        Device 对象指针
 * @param hw         硬件上下文指针
 * @param data       待发送数据
 * @param len        字节数
 * @param timeout_ms 入队等待超时 (ms)
 * @return 成功返回传输字节数, 参数非法返回 VFS_ERR_INVAL, 事务已排队返回 VFS_ERR_BUSY, ESP-IDF 失败返回 VFS_ERR_IO
 */
int spi_slave_queue_tx_internal(struct hal_spi_dev* dev, struct hal_spi_hw* hw,
                                const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    int hw_idx;
    int ret;

    if (!dev || !dev->ctlr || !hw || !dev->ctlr->hw_inited || !data || len == 0)
        return VFS_ERR_INVAL;

    if (atomic_load_explicit(&hw->u.slave.trans_queued, memory_order_acquire))
        return VFS_ERR_BUSY;

    hw_idx = spi_dev_hw_idx(dev);
    if (hw_idx < 0)
        return VFS_ERR_INVAL;

    ret = spi_slave_setup_trans(dev, hw, len, data, s_spi_dummy_rx_buf[hw_idx]);
    if (ret != VFS_OK)
        return ret;

    if (spi_slave_queue_trans(spi_host_id(dev->ctlr), &hw->u.slave.trans,
                              osal_timeout_to_ticks(timeout_ms)) != ESP_OK)
        return VFS_ERR_IO;

    atomic_store_explicit(&hw->u.slave.trans_queued, true, memory_order_release);
    return (int)len;
}
/*===========================================================================================================================================================*/

                                                            /*Master 平台实现*/
/*===========================================================================================================================================================*/
/* 前向声明: spi_master_device_add 引用 post_cb, 实现在 spi_master_transmit 之后 */
static void spi_master_post_cb(spi_transaction_t* trans);
/**
 * @brief SPI Master 总线初始化: 调 spi_bus_initialize 注册 ESP-IDF master bus (含引脚/DMA 通道)
 * @param host Host 对象指针
 * @return 成功返回 VFS_OK, host 为空或已初始化返回 VFS_OK, ESP-IDF 失败返回 VFS_ERR_IO
 */
static int spi_master_bus_init(struct hal_spi_bus_host* host)
{
    const struct hal_spi_bus_config* bus_cfg;

    if (!host || host->hw_inited)
        return VFS_OK;

    bus_cfg = &host->cfg;

    spi_bus_config_t idf_bus_cfg =
    {
        .mosi_io_num = bus_cfg->mosi.pin,
        .miso_io_num = bus_cfg->miso.pin,
        .sclk_io_num = bus_cfg->sclk.pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = bus_cfg->max_transfer_sz > 0 ?
                           bus_cfg->max_transfer_sz : 4096,
    };

    spi_dma_chan_t dma_chan = bus_cfg->dma_chan >= 0 ?
                              (spi_dma_chan_t)bus_cfg->dma_chan : SPI_DMA_CH_AUTO;

    esp_err_t err = spi_bus_initialize(spi_host_id(host), &idf_bus_cfg, dma_chan);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        SYS_LOGE(kTag, "spi_bus_initialize failed: %d", (int)err);
        return VFS_ERR_IO;
    }

    host->hw_inited = 1;
    return VFS_OK;
}

/**
 * @brief SPI Master 总线反初始化: 引用计数为 0 时调 spi_bus_free 释放总线
 * @param host Host 对象指针
 * @return 成功返回 VFS_OK, host 为空或未初始化返回 VFS_OK, 引用未释放返回 VFS_ERR_BUSY, ESP-IDF 失败返回 VFS_ERR_IO
 */
static int spi_master_bus_deinit(struct hal_spi_bus_host* host)
{
    if (!host || !host->hw_inited)
        return VFS_OK;

    if (host->ref_count > 0)
        return VFS_ERR_BUSY;

    if (spi_bus_free(spi_host_id(host)) != ESP_OK)
    {
        SYS_LOGE(kTag, "spi_bus_free failed");
        return VFS_ERR_IO;
    }

    host->hw_inited = 0;
    __builtin_memset(&host->active_cfg, 0, sizeof(host->active_cfg));
    return VFS_OK;
}

/**
 * @brief 在已初始化的总线上挂载一个 master device: 调 spi_bus_add_device (含 clock/mode/CS/queue_size/post_cb)
 * @param dev     Device 对象指针 (用于取 ctlr)
 * @param hw      硬件上下文指针 (用于回填 handle)
 * @param dev_cfg 设备配置 (clock_speed_hz/mode/cs_pin/queue_size)
 * @return 成功返回 VFS_OK, ESP-IDF 失败返回 VFS_ERR_IO
 */
static int spi_master_device_add(struct hal_spi_dev* dev, struct hal_spi_hw* hw,
                                 const struct hal_spi_device_config* dev_cfg)
{
    spi_device_interface_config_t devcfg =
    {
        .clock_speed_hz = dev_cfg->clock_speed_hz > 0 ? dev_cfg->clock_speed_hz : 1000000,
        .mode = (uint8_t)dev_cfg->mode,
        .spics_io_num = dev_cfg->cs_pin,
        .queue_size = dev_cfg->queue_size > 0 ? dev_cfg->queue_size : 4,
        .post_cb = spi_master_post_cb,
    };

    esp_err_t err = spi_bus_add_device(spi_host_id(dev->ctlr), &devcfg, &hw->u.master.handle);
    if (err != ESP_OK)
    {
        SYS_LOGE(kTag, "spi_bus_add_device failed: %d", (int)err);
        return VFS_ERR_IO;
    }

    return VFS_OK;
}

/**
 * @brief SPI Master 硬件初始化: 总线初始化 + (handle 已存在则跳过) 挂载 device
 * @param host     Host 对象指针
 * @param dev      Device 对象指针
 * @param hw       硬件上下文指针
 * @param dev_cfg  设备配置
 * @return 成功返回 VFS_OK, 任一步失败返回对应 VFS_ERR_*
 */
static int spi_master_hw_init(struct hal_spi_bus_host* host, struct hal_spi_dev* dev,
                              struct hal_spi_hw* hw,
                              const struct hal_spi_device_config* dev_cfg)
{
    int ret;

    ret = spi_master_bus_init(host);
    if (ret != VFS_OK)
        return ret;

    if (hw->u.master.handle)
        return VFS_OK;

    ret = spi_master_device_add(dev, hw, dev_cfg);
    if (ret != VFS_OK)
        return ret;

    return VFS_OK;
}

/**
 * @brief SPI Master 同步发送: 填 spi_transaction_t 后调 spi_device_transmit 阻塞等待完成
 * @param hw  硬件上下文指针 (用于取 master handle)
 * @param tx  发送缓冲 (可为 NULL)
 * @param rx  接收缓冲 (可为 NULL)
 * @param len 字节数
 * @return 成功返回传输字节数, ESP-IDF 失败返回 VFS_ERR_IO
 */
static int spi_master_transmit(struct hal_spi_hw* hw, const uint8_t* tx, uint8_t* rx, size_t len)
{
    spi_transaction_t trans =
    {
        .length = len * 8U,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };

    if (spi_device_transmit(hw->u.master.handle, &trans) != ESP_OK)
        return VFS_ERR_IO;

    return (int)len;
}

/*===========================================================================================================================================================*/
                                                              /*Master async (ISR callback)*/
/*===========================================================================================================================================================*/
/* post_cb: IDF 在 DMA done 中断中调用, 通过 trans->user 取回 wrapper */
/**
 * @brief SPI Master DMA done ISR 回调: 从 trans->user 取回 wrapper 并调用户 callback (ISR 上下文, 严禁阻塞)
 * @param trans 已完成的 ESP-IDF 事务指针
 */
static void spi_master_post_cb(spi_transaction_t* trans)
{
    struct hal_spi_trans* wrapper;

    if (!trans)
        return;

    wrapper = (struct hal_spi_trans*)trans->user;
    if (!wrapper || !wrapper->cb)
        return;

    /* ISR 上下文: 调用户 callback, 严禁阻塞 */
    wrapper->cb(wrapper->dev, trans, wrapper->userdata);
}

/* 从 hw_idx 对应的池中取空闲 trans slot */
/**
 * @brief 从 dev 对应的 async trans 池中取一个空闲 slot (标记 in_use)
 * @param dev Device 对象指针
 * @return 空闲 hal_spi_trans 指针, 无可用 slot 或 hw_idx 非法返回 NULL
 */
static struct hal_spi_trans* spi_trans_alloc(struct hal_spi_dev* dev)
{
    int hw_idx = spi_dev_hw_idx(dev);
    int i;

    if (hw_idx < 0)
        return NULL;

    for (i = 0; i < HAL_SPI_MAX_ASYNC; i++)
    {
        if (!s_spi_trans_pool[hw_idx][i].in_use)
        {
            s_spi_trans_pool[hw_idx][i].in_use = 1;
            return &s_spi_trans_pool[hw_idx][i];
        }
    }
    return NULL;
}

/**
 * @brief SPI Master 异步传输: 应用配置 → 取 hw + 分配 wrapper → spi_device_queue_trans 入队
 * @param dev      Device 对象指针 (必须已 hw_open 且 host 为 master)
 * @param tx       发送缓冲 (可为 NULL)
 * @param rx       接收缓冲 (可为 NULL)
 * @param len      字节数
 * @param cb       传输完成 ISR 回调 (可为 NULL)
 * @param userdata 透传给 cb 的用户数据
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 取 hw/handle 失败返回 VFS_ERR_IO, 无空闲 slot 返回 VFS_ERR_BUSY
 */
int hal_spi_transfer_async(struct hal_spi_dev* dev,
                           const uint8_t* tx, uint8_t* rx,
                           size_t len, hal_spi_callback_t cb,
                           void* userdata)
{
    struct hal_spi_hw* hw;
    struct hal_spi_trans* wrapper;
    int ret;

    if (!dev || !dev->ctlr || !dev->hw_open || len == 0)
        return VFS_ERR_INVAL;

    if (dev->ctlr->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    if (len > spi_ctlr_max_transfer_bytes(dev->ctlr))
        return VFS_ERR_INVAL;

    ret = spi_controller_apply_dev_cfg(dev);
    if (ret != VFS_OK)
        return ret;

    hw = spi_dev_hw(dev);
    if (!hw || !hw->u.master.handle)
        return VFS_ERR_IO;

    wrapper = spi_trans_alloc(dev);
    if (!wrapper)
        return VFS_ERR_BUSY;

    __builtin_memset(&wrapper->idf_trans, 0, sizeof(wrapper->idf_trans));
    wrapper->idf_trans.length    = len * 8U;
    wrapper->idf_trans.tx_buffer = tx;
    wrapper->idf_trans.rx_buffer = rx;
    wrapper->idf_trans.user      = wrapper;   /* post_cb 通过此字段取回 wrapper */
    wrapper->cb                 = cb;
    wrapper->userdata           = userdata;
    wrapper->dev                = dev;

    esp_err_t err = spi_device_queue_trans(hw->u.master.handle,
                                           &wrapper->idf_trans,
                                           osal_timeout_to_ticks(OSAL_LOCK_TIMEOUT_DEFAULT_MS));
    if (err != ESP_OK)
    {
        wrapper->in_use = 0;
        SYS_LOGE(kTag, "spi_device_queue_trans failed: %d", (int)err);
        return VFS_ERR_IO;
    }

    return VFS_OK;
}

/**
 * @brief SPI Master 异步轮询取回: 调 spi_device_get_trans_result 阻塞等待一个事务完成 + 归还 wrapper 到池
 * @param dev        Device 对象指针 (必须已 hw_open 且 host 为 master)
 * @param timeout_ms 等待超时 (ms)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 超时返回 VFS_ERR_BUSY, ESP-IDF 失败返回 VFS_ERR_IO
 */
int hal_spi_transfer_poll(struct hal_spi_dev* dev, uint32_t timeout_ms)
{
    struct hal_spi_hw* hw;
    spi_transaction_t* done = NULL;
    struct hal_spi_trans* wrapper;

    if (!dev || !dev->ctlr || !dev->hw_open)
        return VFS_ERR_INVAL;

    if (dev->ctlr->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    hw = spi_dev_hw(dev);
    if (!hw || !hw->u.master.handle)
        return VFS_ERR_IO;

    esp_err_t err = spi_device_get_trans_result(hw->u.master.handle, &done,
                                                 osal_timeout_to_ticks(timeout_ms));
    if (err == ESP_ERR_TIMEOUT)
        return VFS_ERR_BUSY;
    if (err != ESP_OK || !done)
    {
        SYS_LOGE(kTag, "spi_device_get_trans_result failed: %d", (int)err);
        return VFS_ERR_IO;
    }

    /* 归还 trans 到池 */
    wrapper = (struct hal_spi_trans*)done->user;
    if (wrapper)
        wrapper->in_use = 0;

    return VFS_OK;
}

/**
 * @brief SPI Master controller 传输执行: 选择内部 tx/rx dummy 缓冲后转 spi_master_transmit
 * @param host Host 对象指针 (用于校验与取 max_transfer)
 * @param hw   硬件上下文指针 (必须 is_master)
 * @param tx   发送缓冲 (可为 NULL, 用内部 dummy)
 * @param rx   接收缓冲 (可为 NULL, 用内部 dummy)
 * @param len  字节数
 * @return 成功返回传输字节数, 参数非法返回 VFS_ERR_INVAL
 */
int spi_controller_xfer(struct hal_spi_bus_host* host, struct hal_spi_hw* hw,
                        const uint8_t* tx, uint8_t* rx, size_t len)
{
    int hw_idx;

    if (!host || !hw || !hw->is_master || len == 0)
        return VFS_ERR_INVAL;

    hw_idx = (int)(hw - s_spi_hw);
    if (hw_idx < SPI_SLAVE_DEVICE_COUNT || hw_idx >= SPI_DEVICE_COUNT ||
        len > spi_host_max_transfer_bytes(host))
        return VFS_ERR_INVAL;

    const uint8_t* tx_p = tx ? tx : s_spi_tx_buf[hw_idx];
    uint8_t*       rx_p = rx ? rx : s_spi_rx_buf[hw_idx];

    if (!tx)
        __builtin_memset(s_spi_tx_buf[hw_idx], 0, len);

    return spi_master_transmit(hw, tx_p, rx_p, len);
}

/**
 * @brief Master device 配置应用 stub: ESP32 master 的配置在 spi_bus_add_device 时已固化, 此处保留接口空实现
 * @param dev Device 对象指针 (未使用)
 * @return 固定返回 VFS_OK
 */
int spi_controller_apply_dev_cfg(struct hal_spi_dev* dev)
{
    COMPAT_IGNORE_RESULT(dev);
    return VFS_OK;
}
/*===========================================================================================================================================================*/

                                                            /*Host 管理 API*/
/*===========================================================================================================================================================*/
/* 对象由 bus 层提供 (嵌入), HAL 无池管理。hw_idx 在 ESP32 不使用 (设备级池由 pool_idx 索引)。 */
/**
 * @brief SPI Host 初始化: 清零 + 拷贝配置 + 缓存 spi + 校正 bus_role/max_transfer_sz + 标记 bus_ready (ESP32 不在此初始化硬件)
 * @param host  Host 对象指针 (由 bus 层嵌入)
 * @param hw_idx HW slot 索引 (ESP32 未使用)
 * @param cfg   总线配置 (spi/mosi/miso/sclk/bus_role/max_transfer_sz)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, max_transfer_sz 超限返回 VFS_ERR_INVAL
 */
int hal_spi_bus_host_init(struct hal_spi_bus_host* host, int hw_idx,
                          const struct hal_spi_bus_config* cfg)
{
    COMPAT_IGNORE_RESULT(hw_idx);

    if (!host || !cfg)
        return VFS_ERR_INVAL;
    if (host->bus_ready)
        return VFS_OK;

    __builtin_memset(host, 0, sizeof(*host));
    host->cfg = *cfg;
    host->spi = cfg->spi;   /* fast path 缓存 */
    if (host->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER &&
        host->cfg.bus_role != HAL_SPI_BUS_ROLE_SLAVE)
        host->cfg.bus_role = HAL_SPI_BUS_ROLE_SLAVE;
    if (host->cfg.max_transfer_sz <= 0)
        host->cfg.max_transfer_sz = (int)HAL_SPI_MAX_TRANSFER_BYTES;
    else if (host->cfg.max_transfer_sz > (int)HAL_SPI_MAX_TRANSFER_BYTES)
    {
        SYS_LOGE(kTag, "max_transfer_sz %d exceeds HAL limit %d",
                 host->cfg.max_transfer_sz, (int)HAL_SPI_MAX_TRANSFER_BYTES);
        return VFS_ERR_INVAL;
    }

    host->bus_ready = 1;
    return VFS_OK;
}

/**
 * @brief SPI Host 反初始化: 引用计数为 0 时按角色调 master/slave 反初始化, 关闭硬件
 * @param host Host 对象指针
 * @return 成功返回 VFS_OK, host 为空或未就绪直接返回 VFS_OK, 引用未释放返回 VFS_ERR_BUSY
 */
int hal_spi_bus_host_deinit(struct hal_spi_bus_host* host)
{
    if (!host || !host->bus_ready)
        return VFS_OK;

    if (host->ref_count > 0)
    {
        SYS_LOGW(kTag, "host deinit with ref_count=%d", host->ref_count);
        return VFS_ERR_BUSY;
    }

    if (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
        (void)spi_master_bus_deinit(host);
    else
        (void)spi_slave_hw_deinit(host);

    host->bus_ready = 0;
    return VFS_OK;
}
/*===========================================================================================================================================================*/

                                                            /*Device 管理 API*/
/*===========================================================================================================================================================*/
/**
 * @brief SPI Device 对象初始化: 清零 + 绑定 host + 拷贝 device 配置 + 初始化 hw slot (硬件尚未打开)
 * @param dev      Device 对象指针
 * @param pool_idx 设备在 bus 层池中的索引
 * @param host     所属 Host 对象指针
 * @param dev_cfg  设备配置 (mode/clock_speed/cs_pin/queue_size)
 */
void hal_spi_dev_init(struct hal_spi_dev* dev, int pool_idx,
                      struct hal_spi_bus_host* host,
                      const struct hal_spi_device_config* dev_cfg)
{
    if (!dev || !host || !dev_cfg || pool_idx < 0)
        return;

    if (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
    {
        if (pool_idx >= SPI_MASTER_DEVICE_COUNT)
            return;
    }
    else if (pool_idx >= SPI_SLAVE_DEVICE_COUNT)
        return;

    __builtin_memset(dev, 0, sizeof(*dev));
    dev->pool_idx = pool_idx;
    dev->ctlr     = host;
    dev->cfg      = *dev_cfg;

    spi_dev_hw_slot_init(pool_idx, host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER);
}

/**
 * @brief 打开 SPI Device 硬件: master 角色调 spi_master_hw_init (含 bus+device 注册); slave 角色调 spi_slave_hw_init + 校验同一 host 不可挂多 device
 * @param dev Device 对象指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, master 失败返回对应 VFS_ERR_*, slave 已有不同 device 返回 VFS_ERR_BUSY
 */
int hal_spi_dev_hw_open(struct hal_spi_dev* dev)
{
    struct hal_spi_bus_host* host;
    struct hal_spi_hw* hw;
    int ret;

    if (!dev || !dev->ctlr)
        return VFS_ERR_INVAL;

    if (dev->hw_open)
        return VFS_OK;

    host = dev->ctlr;
    hw = spi_dev_hw(dev);
    if (!host->bus_ready || !hw)
        return VFS_ERR_INVAL;

    if (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER)
    {
        ret = spi_master_hw_init(host, dev, hw, &dev->cfg);
        if (ret != VFS_OK)
            return ret;

        dev->hw_open = 1;
        host->ref_count++;
        return VFS_OK;
    }

    if (host->hw_inited &&
        (host->active_cfg.cs_pin != dev->cfg.cs_pin ||
         host->active_cfg.mode != dev->cfg.mode))
    {
        SYS_LOGE(kTag, "ESP32 slave: cannot attach second device on host %d",
                 (int)host->cfg.spi);
        return VFS_ERR_BUSY;
    }

    ret = spi_slave_hw_init(host, &dev->cfg);
    if (ret != VFS_OK)
        return ret;

    dev->hw_open = 1;
    host->ref_count++;
    return VFS_OK;
}

/**
 * @brief 关闭 SPI Device 硬件: 减少 host 引用计数 + master 角色移除 device handle + 标记 hw_open=0
 * @param dev Device 对象指针
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 未打开直接返回 VFS_OK
 */
int hal_spi_dev_hw_close(struct hal_spi_dev* dev)
{
    struct hal_spi_bus_host* host;
    struct hal_spi_hw* hw;

    if (!dev || !dev->ctlr)
        return VFS_ERR_INVAL;

    if (!dev->hw_open)
        return VFS_OK;

    host = dev->ctlr;
    hw = spi_dev_hw(dev);
    if (!host->bus_ready || host->ref_count <= 0)
        return VFS_ERR_INVAL;

    host->ref_count--;

    if (host->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER && hw && hw->u.master.handle)
    {
        COMPAT_IGNORE_RESULT(spi_bus_remove_device(hw->u.master.handle));
        hw->u.master.handle = NULL;
    }

    dev->hw_open = 0;
    return VFS_OK;
}

/**
 * @brief 取回 slave 异步事务结果: 调 spi_slave_get_trans_result 阻塞等待 + 复位 trans_queued + 拷贝 rx 数据到调用者缓冲
 * @param dev        Device 对象指针 (必须已 hw_open 且 host 为 slave, 且有已排队事务)
 * @param rx_data    调用者接收缓冲 (可为 NULL, 仅查字节数)
 * @param rx_cap     rx_data 容量
 * @param trans_len  回传实际接收字节数 (可为 NULL)
 * @param timeout_ms 等待超时 (ms)
 * @return 成功返回 VFS_OK, 参数非法返回 VFS_ERR_INVAL, 超时返回 VFS_ERR_BUSY, ESP-IDF 失败返回 VFS_ERR_IO, rx 容量不足返回 VFS_ERR_NOMEM
 */
int hal_spi_get_trans_result(struct hal_spi_dev* dev, uint8_t* rx_data, size_t rx_cap,
                             size_t* trans_len, uint32_t timeout_ms)
{
    struct hal_spi_hw* hw;
    spi_slave_transaction_t* done = NULL;

    hw = spi_dev_hw(dev);
    if (!dev || !dev->ctlr || !hw || !dev->ctlr->hw_inited ||
        dev->ctlr->cfg.bus_role == HAL_SPI_BUS_ROLE_MASTER ||
        !atomic_load_explicit(&hw->u.slave.trans_queued, memory_order_acquire))
        return VFS_ERR_INVAL;

    esp_err_t err = spi_slave_get_trans_result(spi_host_id(dev->ctlr), &done,
                                               osal_timeout_to_ticks(timeout_ms));

    if (err == ESP_ERR_TIMEOUT)
        return VFS_ERR_BUSY;
    if (err != ESP_OK || !done)
    {
        SYS_LOGE(kTag, "spi_slave_get_trans_result failed: %d", (int)err);
        return VFS_ERR_IO;
    }

    atomic_store_explicit(&hw->u.slave.trans_queued, false, memory_order_release);

    size_t rx_bytes = (done->trans_len + 7U) / 8U;
    if (trans_len)
        *trans_len = rx_bytes;

    if (rx_data && rx_cap > 0U && rx_bytes > 0U)
    {
        int hw_idx = spi_dev_hw_idx(dev);
        if (hw_idx < 0)
            return VFS_ERR_IO;

        if (rx_bytes > rx_cap)
        {
            SYS_LOGW(kTag, "rx buffer overflow! host sent %zu bytes, cap is %zu",
                     rx_bytes, rx_cap);
            return VFS_ERR_NOMEM;
        }

        size_t copy_len = rx_bytes < rx_cap ? rx_bytes : rx_cap;
        __builtin_memcpy(rx_data, s_spi_dummy_rx_buf[hw_idx], copy_len);
    }

    return VFS_OK;
}

/*============================================================================*/
/*                              DMA 强制中止 (panic/reboot 路径, 空实现)       */
/*============================================================================*/
/* ESP32 GDMA 自动管理, 无需 SPI 介入, 空实现满足统一头。 */
void hal_spi_dma_abort(void)
{
}
/*===========================================================================================================================================================*/
