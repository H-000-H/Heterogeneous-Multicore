#include "w25q64_drv.h"
#include "bus.h"
#include "hal_spi.h"
#include "hal_spi_bus_host.h"
#include "device.h"
#include "driver.h"
#include "dev_lifecycle.h"
#include "VFS.h"
#include "dt_config_gen.h"
#include "board_config.h"
#include "compiler_compat.h"
#include "osal.h"
#include "system_log.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define W25Q64_COUNT           DTC_GEN_COUNT_HETEROGENEOUS_W25Q64_MASTER
#define W25Q64_XFER_BUF_SIZE   512U
#define W25Q64_CMD_ADDR_SIZE   3U
#define W25Q64_CMD_HDR_SIZE    (1U + W25Q64_CMD_ADDR_SIZE)

#define W25Q64_SPI_OP_WRITE_ENABLE       0x06U
#define W25Q64_SPI_OP_READ_STATUS1       0x05U
#define W25Q64_SPI_OP_PAGE_PROGRAM       0x02U
#define W25Q64_SPI_OP_SECTOR_ERASE_4K    0x20U
#define W25Q64_SPI_OP_READ_DATA          0x03U
#define W25Q64_SPI_OP_JEDEC_ID           0x9FU
#define W25Q64_STATUS_WIP                0x01U

struct w25q64_device
{
    struct file_operations ops;
    struct hal_spi_ctx     ctx;
    struct osal_mutex*     io_mutex;
    int                    pool_idx;
    size_t                 max_xfer;
    uint32_t               f_pos;
    uint8_t                jedec_id[W25Q64_JEDEC_ID_LEN];
};

static struct w25q64_device s_w25q64_pool[W25Q64_COUNT];
static uint8_t              s_w25q64_used[W25Q64_COUNT];
static osal_pool_t          s_w25q64_pool_ctrl;
static uint8_t s_w25q64_mutex_storage[W25Q64_COUNT][OSAL_MUTEX_STORAGE_SIZE];
static uint8_t s_w25q64_xfer_buf[W25Q64_COUNT][W25Q64_CMD_HDR_SIZE + W25Q64_XFER_BUF_SIZE];

static const char* const kTag = "w25q64";

pre_execution(160)
static void w25q64_pool_boot_init(void)
{
    osal_pool_init(&s_w25q64_pool_ctrl, s_w25q64_used, W25Q64_COUNT);
}

static struct w25q64_device* w25q64_get_drvdata(struct device* dev)
{
    return (struct w25q64_device*)device_get_priv(dev);
}

static int w25q64_wait_ready(struct w25q64_device* flash, uint32_t timeout_ms)
{
    uint8_t tx[2] = { W25Q64_SPI_OP_READ_STATUS1, 0x00 };
    uint8_t rx[2] = { 0, 0 };
    uint32_t elapsed = 0;
    const uint32_t step_ms = 1U;

    while (elapsed <= timeout_ms)
    {
        if (hal_spi_transfer(&flash->ctx, tx, rx, sizeof(tx), timeout_ms) != VFS_OK)
            return VFS_ERR_IO;

        if ((rx[1] & W25Q64_STATUS_WIP) == 0U)
            return VFS_OK;

        osal_delay_ms(step_ms);
        elapsed += step_ms;
    }

    return VFS_ERR_BUSY;
}

static int w25q64_write_enable(struct w25q64_device* flash, uint32_t timeout_ms)
{
    uint8_t cmd = W25Q64_SPI_OP_WRITE_ENABLE;
    return hal_spi_transfer(&flash->ctx, &cmd, NULL, 1U, timeout_ms);
}

static int w25q64_hw_read_jedec(struct w25q64_device* flash, uint8_t id[W25Q64_JEDEC_ID_LEN],
                             uint32_t timeout_ms)
{
    uint8_t tx[4] = { W25Q64_SPI_OP_JEDEC_ID, 0x00, 0x00, 0x00 };
    uint8_t rx[4] = { 0, 0, 0, 0 };

    if (hal_spi_transfer(&flash->ctx, tx, rx, sizeof(tx), timeout_ms) != VFS_OK)
        return VFS_ERR_IO;

    id[0] = rx[1];
    id[1] = rx[2];
    id[2] = rx[3];
    return VFS_OK;
}

static int w25q64_hw_read_data(struct w25q64_device* flash, uint32_t addr,
                            uint8_t* data, size_t len, uint32_t timeout_ms)
{
    uint8_t* frame;
    size_t chunk;
    size_t offset = 0U;

    if (!data || len == 0U)
        return VFS_ERR_INVAL;
    if ((uint64_t)addr + (uint64_t)len > W25Q64_FLASH_SIZE)
        return VFS_ERR_INVAL;

    frame = s_w25q64_xfer_buf[flash->pool_idx];
    chunk = flash->max_xfer;
    if (chunk == 0U || chunk > W25Q64_XFER_BUF_SIZE)
        chunk = W25Q64_XFER_BUF_SIZE;

    while (offset < len)
    {
        size_t n = len - offset;
        uint32_t cur_addr = addr + (uint32_t)offset;

        if (n > chunk)
            n = chunk;

        frame[0] = W25Q64_SPI_OP_READ_DATA;
        frame[1] = (uint8_t)((cur_addr >> 16) & 0xFFU);
        frame[2] = (uint8_t)((cur_addr >> 8) & 0xFFU);
        frame[3] = (uint8_t)(cur_addr & 0xFFU);
        __builtin_memset(frame + W25Q64_CMD_HDR_SIZE, 0, n);

        if (hal_spi_transfer(&flash->ctx, frame, frame, W25Q64_CMD_HDR_SIZE + n,
                             timeout_ms) != VFS_OK)
            return VFS_ERR_IO;

        __builtin_memcpy(data + offset, frame + W25Q64_CMD_HDR_SIZE, n);
        offset += n;
    }

    return VFS_OK;
}

static int w25q64_hw_page_program(struct w25q64_device* flash, uint32_t addr,
                               const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    uint8_t* frame;

    if (!data || len == 0U || len > W25Q64_PAGE_SIZE)
        return VFS_ERR_INVAL;
    if ((addr & (W25Q64_PAGE_SIZE - 1U)) + len > W25Q64_PAGE_SIZE)
        return VFS_ERR_INVAL;
    if ((uint64_t)addr + (uint64_t)len > W25Q64_FLASH_SIZE)
        return VFS_ERR_INVAL;

    frame = s_w25q64_xfer_buf[flash->pool_idx];
    frame[0] = W25Q64_SPI_OP_PAGE_PROGRAM;
    frame[1] = (uint8_t)((addr >> 16) & 0xFFU);
    frame[2] = (uint8_t)((addr >> 8) & 0xFFU);
    frame[3] = (uint8_t)(addr & 0xFFU);
    __builtin_memcpy(frame + W25Q64_CMD_HDR_SIZE, data, len);

    if (w25q64_write_enable(flash, timeout_ms) != VFS_OK)
        return VFS_ERR_IO;

    if (hal_spi_transfer(&flash->ctx, frame, NULL, W25Q64_CMD_HDR_SIZE + len,
                         timeout_ms) != VFS_OK)
        return VFS_ERR_IO;

    return w25q64_wait_ready(flash, timeout_ms);
}

static int w25q64_hw_write_data(struct w25q64_device* flash, uint32_t addr,
                             const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    size_t offset = 0U;

    if (!data || len == 0U)
        return VFS_ERR_INVAL;
    if ((uint64_t)addr + (uint64_t)len > W25Q64_FLASH_SIZE)
        return VFS_ERR_INVAL;

    while (offset < len)
    {
        uint32_t cur_addr = addr + (uint32_t)offset;
        size_t page_room = W25Q64_PAGE_SIZE - (cur_addr & (W25Q64_PAGE_SIZE - 1U));
        size_t n = len - offset;

        if (n > page_room)
            n = page_room;

        if (w25q64_hw_page_program(flash, cur_addr, data + offset, n, timeout_ms) != VFS_OK)
            return VFS_ERR_IO;

        offset += n;
    }

    return VFS_OK;
}

static int w25q64_hw_sector_erase(struct w25q64_device* flash, uint32_t addr, uint32_t timeout_ms)
{
    uint8_t cmd[4];

    if ((addr & (W25Q64_SECTOR_SIZE - 1U)) != 0U)
        return VFS_ERR_INVAL;
    if (addr >= W25Q64_FLASH_SIZE)
        return VFS_ERR_INVAL;

    cmd[0] = W25Q64_SPI_OP_SECTOR_ERASE_4K;
    cmd[1] = (uint8_t)((addr >> 16) & 0xFFU);
    cmd[2] = (uint8_t)((addr >> 8) & 0xFFU);
    cmd[3] = (uint8_t)(addr & 0xFFU);

    if (w25q64_write_enable(flash, timeout_ms) != VFS_OK)
        return VFS_ERR_IO;

    if (hal_spi_transfer(&flash->ctx, cmd, NULL, sizeof(cmd), timeout_ms) != VFS_OK)
        return VFS_ERR_IO;

    return w25q64_wait_ready(flash, timeout_ms);
}

static int w25q64_open(struct device* dev, void* arg)
{
    struct w25q64_device* flash;
    struct dev_lifecycle* lc;
    int first;
    int ret;

    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    flash = w25q64_get_drvdata(dev);
    if (IS_ERR(flash))
        return PTR_ERR(flash);

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (first < 0)
        return first;

    ret = VFS_OK;
    if (first == 1)
    {
        flash->f_pos = 0U;
        ret = hal_spi_interface_attach(&flash->ctx);
        if (ret != VFS_OK)
            dev_lc_open_abort(lc);
        else
            flash->ctx.attached = 1;
    }

    if (ret == VFS_OK)
    {
        dev_lc_open_end(lc);
        if (first == 1)
        {
            uint8_t jedec[W25Q64_JEDEC_ID_LEN];
            if (w25q64_hw_read_jedec(flash, jedec, OSAL_LOCK_TIMEOUT_DEFAULT_MS) == VFS_OK)
            {
                __builtin_memcpy(flash->jedec_id, jedec, sizeof(flash->jedec_id));
                SYS_LOGI(kTag, "open OK: jedec=%02X%02X%02X",
                         jedec[0], jedec[1], jedec[2]);
            }
        }
    }

    return ret;
}

static int w25q64_close(struct device* dev)
{
    struct w25q64_device* flash;
    struct dev_lifecycle* lc;
    int last;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    flash = w25q64_get_drvdata(dev);
    if (IS_ERR(flash))
        return PTR_ERR(flash);

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (last < 0)
        return last;

    if (last && flash->ctx.attached)
    {
        COMPAT_IGNORE_RESULT(hal_spi_interface_detach(&flash->ctx));
        flash->ctx.attached = 0;
    }

    dev_lc_close_end(lc);
    return VFS_OK;
}

static int w25q64_read(struct device* dev, void* buffer, size_t len, uint32_t timeout_ms)
{
    struct w25q64_device* flash;
    struct dev_lifecycle* lc;
    int ret;

    if (!dev)
        return VFS_ERR_INVAL;

    flash = w25q64_get_drvdata(dev);
    if (IS_ERR(flash))
        return PTR_ERR(flash);

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    if (len == 0U)
    {
        dev_lc_io_end(lc);
        return 0;
    }
    if (!buffer)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }
    if ((uint64_t)flash->f_pos + (uint64_t)len > W25Q64_FLASH_SIZE)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    ret = w25q64_hw_read_data(flash, flash->f_pos, (uint8_t*)buffer, len, timeout_ms);
    if (ret == VFS_OK)
    {
        flash->f_pos += (uint32_t)len;
        ret = (int)len;
    }

    dev_lc_io_end(lc);
    return ret;
}

static int w25q64_write(struct device* dev, const void* buffer, size_t len, uint32_t timeout_ms)
{
    struct w25q64_device* flash;
    struct dev_lifecycle* lc;
    int ret;

    if (!dev)
        return VFS_ERR_INVAL;

    flash = w25q64_get_drvdata(dev);
    if (IS_ERR(flash))
        return PTR_ERR(flash);

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    if (len == 0U)
    {
        dev_lc_io_end(lc);
        return 0;
    }
    if (!buffer)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }
    if ((uint64_t)flash->f_pos + (uint64_t)len > W25Q64_FLASH_SIZE)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    ret = w25q64_hw_write_data(flash, flash->f_pos, (const uint8_t*)buffer, len, timeout_ms);
    if (ret == VFS_OK)
    {
        flash->f_pos += (uint32_t)len;
        ret = (int)len;
    }

    dev_lc_io_end(lc);
    return ret;
}

static int w25q64_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct w25q64_device* flash;
    struct dev_lifecycle* lc;
    int ret;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    flash = w25q64_get_drvdata(dev);
    if (IS_ERR(flash))
        return PTR_ERR(flash);

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK)
        return ret;

    switch (cmd)
    {
    case W25Q64_CMD_SEEK:
    {
        const uint32_t* offset = (const uint32_t*)arg;
        if (!offset || arg_len != sizeof(*offset) || *offset >= W25Q64_FLASH_SIZE)
            ret = VFS_ERR_INVAL;
        else
        {
            flash->f_pos = *offset;
            ret = VFS_OK;
        }
        break;
    }
    case W25Q64_CMD_SECTOR_ERASE:
    {
        const uint32_t* addr = (const uint32_t*)arg;
        if (!addr || arg_len != sizeof(*addr))
            ret = VFS_ERR_INVAL;
        else
            ret = w25q64_hw_sector_erase(flash, *addr, timeout_ms);
        break;
    }
    case W25Q64_CMD_READ_JEDEC_ID:
    {
        struct w25q64_jedec_arg* jedec = (struct w25q64_jedec_arg*)arg;
        if (!jedec || arg_len != sizeof(*jedec))
            ret = VFS_ERR_INVAL;
        else
            ret = w25q64_hw_read_jedec(flash, jedec->id, timeout_ms);
        break;
    }
    default:
        ret = VFS_ERR_INVAL;
        break;
    }

    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations w25q64_fops =
{
    .open  = w25q64_open,
    .close = w25q64_close,
    .read  = w25q64_read,
    .write = w25q64_write,
    .ioctl = w25q64_ioctl,
};

static int w25q64_spi_probe(struct device* dev)
{
    struct w25q64_device* flash;
    struct bus_controller* ctrl;
    struct hal_spi_bus_host* bus_host;
    struct hal_spi_device_config dev_cfg;
    int cs = -1, mode = -1, clock_speed_hz = -1, queue_size = -1;
    int max_trans = -1;
    int pool_idx;
    int ret;

    ret = bus_controller_of(dev, &ctrl);
    if (ret != VFS_OK || !ctrl->hw_priv)
    {
        SYS_LOGE(kTag, "parent bus not ready: %s", device_get_name(dev));
        return ret != VFS_OK ? ret : VFS_ERR_IO;
    }

    bus_host = (struct hal_spi_bus_host*)ctrl->hw_priv;
    if (bus_host->cfg.bus_role != HAL_SPI_BUS_ROLE_MASTER)
    {
        SYS_LOGE(kTag, "w25q64 requires SPI master bus");
        return VFS_ERR_INVAL;
    }

    if (device_get_prop_int(dev, "cs-pin", &cs) ||
        device_get_prop_int(dev, "spi-mode", &mode) ||
        device_get_prop_int(dev, "spi-max-frequency", &clock_speed_hz) ||
        device_get_prop_int(dev, "queue-size", &queue_size))
        goto err_prop;

    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "max-trans-buffer", &max_trans));

    pool_idx = osal_pool_claim(&s_w25q64_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    flash = &s_w25q64_pool[pool_idx];
    __builtin_memset(flash, 0, sizeof(*flash));
    flash->pool_idx = pool_idx;
    flash->max_xfer = (max_trans > 0) ? (size_t)max_trans : W25Q64_XFER_BUF_SIZE;
    if (flash->max_xfer > W25Q64_XFER_BUF_SIZE)
        flash->max_xfer = W25Q64_XFER_BUF_SIZE;

    dev_cfg.mode           = mode;
    dev_cfg.clock_speed_hz = clock_speed_hz;
    dev_cfg.cs_pin         = cs;
    dev_cfg.queue_size     = queue_size;

    hal_spi_ctx_init(&flash->ctx, pool_idx, bus_host, &dev_cfg);
    hal_spi_ctx_attach(&flash->ctx);

    if (osal_mutex_create_static(&flash->io_mutex, s_w25q64_mutex_storage[pool_idx],
                                 sizeof(s_w25q64_mutex_storage[pool_idx])) != 0)
        goto err_pool;

    device_lc_bind(dev, flash->io_mutex);
    flash->ops = w25q64_fops;
    dev->ops   = &flash->ops;

    if (device_set_priv(dev, flash) != VFS_OK)
        goto err_pool;

    COMPAT_IGNORE_RESULT(bus_client_bind(dev, ctrl->dev, flash));

    SYS_LOGI(kTag, "probe OK: cs=%d mode=%d freq=%d", cs, mode, clock_speed_hz);
    return VFS_OK;
err_pool:
    hal_spi_ctx_detach(&flash->ctx);
    osal_mutex_destroy(flash->io_mutex);
    __builtin_memset(flash, 0, sizeof(*flash));
    osal_pool_release(&s_w25q64_pool_ctrl, pool_idx);
    return VFS_ERR_IO;

err_prop:
    SYS_LOGE(kTag, "property error: %s", device_get_name(dev));
    return VFS_ERR_INVAL;
}

static int w25q64_spi_remove(struct device* dev)
{
    struct w25q64_device* flash;
    struct dev_lifecycle* lc;
    struct osal_mutex* io_mutex;
    int pool_idx;

    if (!dev)
        return VFS_ERR_INVAL;

    flash = w25q64_get_drvdata(dev);
    if (IS_ERR(flash))
        return PTR_ERR(flash);

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    io_mutex = flash->io_mutex;
    pool_idx = flash->pool_idx;

    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    bus_client_unbind(dev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
        return VFS_ERR_IO;

    if (flash->ctx.attached)
    {
        COMPAT_IGNORE_RESULT(hal_spi_interface_detach(&flash->ctx));
        flash->ctx.attached = 0;
    }

    hal_spi_ctx_detach(&flash->ctx);
    osal_mutex_destroy(io_mutex);
    __builtin_memset(flash, 0, sizeof(*flash));
    osal_pool_release(&s_w25q64_pool_ctrl, pool_idx);
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(w25q64_spi, "heterogeneous,w25q64-master", w25q64_spi_probe, w25q64_spi_remove)
