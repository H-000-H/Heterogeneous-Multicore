/*
 * SPI core — 统一传输入口 spi_sync / spi_slave_sync / spi_slave_queue_tx
 */
#include "hal_spi.h"
#include "spi_internal.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

static size_t spi_ctlr_max_transfer_bytes(const struct hal_spi_bus_host* ctlr)
{
    if (!ctlr)
        return 0;
    if (ctlr->cfg.max_transfer_sz > 0)
        return (size_t)ctlr->cfg.max_transfer_sz;
    return 2048U;
}

int spi_sync(struct hal_spi_dev* dev, const uint8_t* tx, uint8_t* rx,
             size_t len, uint32_t timeout_ms)
{
    struct hal_spi_hw* hw;
    int                ret;
    int                xfer_ret;

    if (!dev || !dev->ctlr || !dev->hw_open || len == 0)
        return VFS_ERR_INVAL;

    if (dev->ctlr->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER)
        return VFS_ERR_INVAL;

    if (len > spi_ctlr_max_transfer_bytes(dev->ctlr))
        return VFS_ERR_INVAL;

    if (osal_mutex_lock(dev->ctlr->bus_mutex, timeout_ms) != 0)
        return VFS_ERR_BUSY;

    ret = spi_controller_apply_dev_cfg(dev);
    if (ret != VFS_OK)
        goto unlock;

    hw = spi_dev_hw(dev);
    if (!hw)
    {
        ret = VFS_ERR_IO;
        goto unlock;
    }

    xfer_ret = spi_controller_xfer(dev->ctlr, hw, tx, rx, len);
    if (xfer_ret < 0)
        ret = VFS_ERR_IO;
    else
        ret = VFS_OK;

unlock:
    if (osal_mutex_unlock(dev->ctlr->bus_mutex) != 0 && ret == VFS_OK)
        return VFS_ERR_IO;
    return ret;
}

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

    if (osal_mutex_lock(dev->ctlr->bus_mutex, timeout_ms) != 0)
        return VFS_ERR_BUSY;

    ret = spi_slave_bus_reconfigure(dev->ctlr, &dev->cfg);
    if (ret != VFS_OK)
        goto unlock;

    hw = spi_dev_hw(dev);
    if (!hw)
    {
        ret = VFS_ERR_IO;
        goto unlock;
    }

    xfer_ret = spi_slave_controller_xfer(dev, hw, tx, rx, len, timeout_ms);
    if (xfer_ret < 0)
        ret = VFS_ERR_IO;
    else
        ret = VFS_OK;

unlock:
    if (osal_mutex_unlock(dev->ctlr->bus_mutex) != 0 && ret == VFS_OK)
        return VFS_ERR_IO;
    return ret;
}

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

    if (osal_mutex_lock(dev->ctlr->bus_mutex, timeout_ms) != 0)
        return VFS_ERR_BUSY;

    ret = spi_slave_bus_reconfigure(dev->ctlr, &dev->cfg);
    if (ret != VFS_OK)
        goto unlock;

    hw = spi_dev_hw(dev);
    if (!hw)
    {
        ret = VFS_ERR_IO;
        goto unlock;
    }

    xfer_ret = spi_slave_queue_tx_internal(dev, hw, data, len, timeout_ms);
    if (xfer_ret < 0)
        ret = VFS_ERR_IO;
    else
        ret = VFS_OK;

unlock:
    if (osal_mutex_unlock(dev->ctlr->bus_mutex) != 0 && ret == VFS_OK)
        return VFS_ERR_IO;
    return ret;
}
