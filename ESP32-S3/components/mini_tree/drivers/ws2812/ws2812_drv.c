#include "ws2812_drv.h"
#include "device.h"
#include "driver.h"
#include "VFS.h"
#include "dt_config_gen.h"
#include "board_config.h"
#include "hal_pulse_engine.h"
#include "osal.h"
#include "system_log.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

#define WS2812_COUNT  DTC_GEN_COUNT_ESP32_WS2812

struct ws2812_device
{
    struct device*        dev;
    struct hal_pulse_ws2812_hw hw;
    struct osal_mutex*         io_mutex;
    int                   engine_id;
    int                   gpio;
    int                   num_leds;
    int                   pool_idx;
    uint8_t               brightness;
    uint8_t               color_order[3];
    size_t                bytes_per_led;
    uint32_t              default_timeout_ms;
    uint8_t*              tx_buf;
};

static const char* const kTag = "ws2812_drv";

_Static_assert(WS2812_COUNT <= HAL_PULSE_ENGINE_MAX,
               "DTC_GEN_COUNT_ESP32_WS2812 exceeds HAL_PULSE_ENGINE_MAX");
_Static_assert(WS2812_DRV_TX_BUF_MAX <= (size_t)INT_MAX,
               "WS2812_DRV_TX_BUF_MAX must fit in write() return type (int)");

static struct ws2812_device s_ws2812_pool[WS2812_COUNT];
static uint8_t s_ws2812_used[WS2812_COUNT];
static osal_pool_t s_ws2812_pool_ctrl;
static uint8_t s_ws2812_tx_buf[WS2812_COUNT][WS2812_DRV_TX_BUF_MAX];
static uint8_t s_ws2812_mutex_storage[WS2812_COUNT][OSAL_MUTEX_STORAGE_SIZE];

pre_execution(160)
static void ws2812_pool_boot_init(void)
{
    osal_pool_init(&s_ws2812_pool_ctrl, s_ws2812_used, WS2812_COUNT);
}

enum {
    WS2812_COLOR_R = 0,
    WS2812_COLOR_G = 1,
    WS2812_COLOR_B = 2,
};

static int ws2812_parse_color_order(const char* order, uint8_t out[3])
{
    uint8_t seen[3] = { 0, 0, 0 };

    if (!order || strlen(order) != 3)
        return VFS_ERR_INVAL;

    for (int i = 0; i < 3; i++)
    {
        uint8_t component;

        switch (order[i])
        {
        case 'r':
        case 'R':
            component = WS2812_COLOR_R;
            break;
        case 'g':
        case 'G':
            component = WS2812_COLOR_G;
            break;
        case 'b':
        case 'B':
            component = WS2812_COLOR_B;
            break;
        default:
            return VFS_ERR_INVAL;
        }

        if (seen[component])
            return VFS_ERR_INVAL;

        seen[component] = 1;
        out[component] = (uint8_t)i;
    }

    return VFS_OK;
}

static int ws2812_push_frame(struct ws2812_device* wsdev, uint32_t timeout_ms)
{
    size_t frame_len = (size_t)wsdev->num_leds * (size_t)wsdev->bytes_per_led;
    return hal_pulse_ws2812_send(wsdev->engine_id, wsdev->tx_buf, frame_len, timeout_ms);
}

static void ws2812_clear_frame(struct ws2812_device* wsdev)
{
    __builtin_memset(wsdev->tx_buf, 0, (size_t)wsdev->num_leds * (size_t)wsdev->bytes_per_led);
}

static void ws2812_set_pixel(struct ws2812_device* wsdev, int index,
                             uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t* p = &wsdev->tx_buf[(size_t)index * wsdev->bytes_per_led];
    p[wsdev->color_order[WS2812_COLOR_R]] = r;
    p[wsdev->color_order[WS2812_COLOR_G]] = g;
    p[wsdev->color_order[WS2812_COLOR_B]] = b;
}

static uint8_t ws2812_scale8(uint8_t value, uint8_t brightness)
{
    /* FastLED-style scale8: brightness=255 → 满幅; 低亮度步进较陡 (无 gamma). */
    return (uint8_t)(((uint16_t)value * ((uint16_t)brightness + 1U)) >> 8);
}

static int ws2812_set_color(struct ws2812_device* wsdev,
                            const struct ws2812_color* color,
                            uint32_t timeout_ms)
{
    if (!wsdev || !color)
        return VFS_ERR_INVAL;

    uint8_t r = ws2812_scale8(color->r, wsdev->brightness);
    uint8_t g = ws2812_scale8(color->g, wsdev->brightness);
    uint8_t b = ws2812_scale8(color->b, wsdev->brightness);

    for (int i = 0; i < wsdev->num_leds; i++)
        ws2812_set_pixel(wsdev, i, r, g, b);

    return ws2812_push_frame(wsdev, timeout_ms) == 0 ? VFS_OK : VFS_ERR_IO;
}

static struct ws2812_device* ws2812_get_drvdata(struct device* dev)
{
    void* priv;

    if (!dev)
        return (struct ws2812_device*)ERR_PTR(VFS_ERR_INVAL);

    priv = device_get_priv(dev);
    if (IS_ERR(priv))
        return (struct ws2812_device*)priv;

    return (struct ws2812_device*)priv;
}

static int ws2812_open(struct device* dev, void* arg)
{
    (void)arg;
    if (!dev)
        return VFS_ERR_INVAL;

    struct ws2812_device* wsdev = ws2812_get_drvdata(dev);
    if (IS_ERR(wsdev))
        return PTR_ERR(wsdev);

    struct dev_lifecycle* lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    int first = dev_lc_open_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (first < 0)
        return first;

    int ret = VFS_OK;
    if (first)
    {
        ws2812_clear_frame(wsdev);
        ret = ws2812_push_frame(wsdev, wsdev->default_timeout_ms) == 0 ?
              VFS_OK : VFS_ERR_IO;
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }

    dev_lc_open_end(lc);
    return ret;
}

static int ws2812_close(struct device* dev)
{
    if (!dev)
        return VFS_ERR_INVAL;

    struct ws2812_device* wsdev = ws2812_get_drvdata(dev);
    if (IS_ERR(wsdev))
        return PTR_ERR(wsdev);

    struct dev_lifecycle* lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    int last = dev_lc_close_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (last < 0)
        return last;

    if (last)
    {
        ws2812_clear_frame(wsdev);
        /* close 不因灭灯失败而向上报错; 失败时记 log 便于排查硬件 */
        if (ws2812_push_frame(wsdev, wsdev->default_timeout_ms) != 0)
            DRV_LOGW(kTag, "close: off-frame push failed");
    }

    dev_lc_close_end(lc);
    return VFS_OK;
}

static int ws2812_write(struct device* dev, const void* buf, size_t len, uint32_t timeout_ms)
{
    if (!dev)
        return VFS_ERR_INVAL;

    struct ws2812_device* wsdev = ws2812_get_drvdata(dev);
    if (IS_ERR(wsdev))
        return PTR_ERR(wsdev);

    struct dev_lifecycle* lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    int ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK) return ret;

    if (len == 0)
    {
        dev_lc_io_end(lc);
        return VFS_OK;
    }
    if (!buf)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    size_t bpl = wsdev->bytes_per_led;
    size_t max_len = (size_t)wsdev->num_leds * bpl;
    if (len > max_len || (len > 0U && (len % bpl) != 0U))
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }
    memcpy(wsdev->tx_buf, buf, len);
    if (ws2812_push_frame(wsdev, timeout_ms) != 0)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_IO;
    }
    dev_lc_io_end(lc);
    return (int)len;
}

static int ws2812_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    if (!dev)
        return VFS_ERR_INVAL;

    struct ws2812_device* wsdev = ws2812_get_drvdata(dev);
    if (IS_ERR(wsdev))
        return PTR_ERR(wsdev);

    struct dev_lifecycle* lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    int ret = dev_lc_io_begin(lc, OSAL_LOCK_TIMEOUT_DEFAULT_MS);
    if (ret != VFS_OK) return ret;

    switch (cmd)
    {
    case WS2812_CMD_SET_COLOR:
        if (!arg || arg_len != sizeof(struct ws2812_color)) ret = VFS_ERR_INVAL;
        else ret = ws2812_set_color(wsdev, (const struct ws2812_color*)arg, timeout_ms);
        break;
    case WS2812_CMD_SET_BRIGHTNESS:
        if (!arg || arg_len != sizeof(uint8_t))
            ret = VFS_ERR_INVAL;
        else
        {
            wsdev->brightness = *(const uint8_t*)arg;
            ret = VFS_OK;
        }
        break;
    case WS2812_CMD_OFF:
    {
        const struct ws2812_color off = {0, 0, 0};
        ret = ws2812_set_color(wsdev, &off, timeout_ms);
        break;
    }
    default:
        ret = VFS_ERR_INVAL;
        break;
    }

    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations ws2812_fops =
{
    .init = NULL,
    .open = ws2812_open,
    .close = ws2812_close,
    .write = ws2812_write,
    .read = NULL,
    .ioctl = ws2812_ioctl,
    .suspend = NULL,
    .resume = NULL,
};

static int ws2812_probe(struct device* dev)
{
    int gpio, num_leds, brightness, bytes_per_led, default_timeout_ms;
    int rmt_resolution_hz, rmt_mem_block, rmt_queue_depth;
    int t0h_ticks, t0l_ticks, t1h_ticks, t1l_ticks, reset_ticks;
    const char* color_order;

    if (device_get_prop_int(dev, "gpio", &gpio) != 0 ||
        device_get_prop_int(dev, "num-leds", &num_leds) != 0 ||
        device_get_prop_int(dev, "brightness", &brightness) != 0 ||
        device_get_prop_int(dev, "bytes-per-led", &bytes_per_led) != 0 ||
        device_get_prop_str(dev, "color-order", &color_order) != 0 ||
        device_get_prop_int(dev, "default-timeout-ms", &default_timeout_ms) != 0 ||
        device_get_prop_int(dev, "rmt-resolution-hz", &rmt_resolution_hz) != 0 ||
        device_get_prop_int(dev, "rmt-mem-block", &rmt_mem_block) != 0 ||
        device_get_prop_int(dev, "rmt-queue-depth", &rmt_queue_depth) != 0 ||
        device_get_prop_int(dev, "t0h-ticks", &t0h_ticks) != 0 ||
        device_get_prop_int(dev, "t0l-ticks", &t0l_ticks) != 0 ||
        device_get_prop_int(dev, "t1h-ticks", &t1h_ticks) != 0 ||
        device_get_prop_int(dev, "t1l-ticks", &t1l_ticks) != 0 ||
        device_get_prop_int(dev, "reset-ticks", &reset_ticks) != 0)
        {
        return VFS_ERR_INVAL;
    }

    if (num_leds <= 0 || bytes_per_led <= 0)
    {
        return VFS_ERR_INVAL;
    }

    if (brightness < 0 || brightness > 255)
    {
        return VFS_ERR_INVAL;
    }

    {
        size_t frame_len = (size_t)num_leds * (size_t)bytes_per_led;
        if (frame_len > WS2812_DRV_TX_BUF_MAX)
        {
            return VFS_ERR_INVAL;
        }
    }

    int pool_idx = osal_pool_claim(&s_ws2812_pool_ctrl);
    if (pool_idx < 0) return VFS_ERR_NOMEM;

    struct ws2812_device* wsdev = &s_ws2812_pool[pool_idx];
    __builtin_memset(wsdev, 0, sizeof(*wsdev));
    wsdev->dev = dev;
    wsdev->engine_id = pool_idx;
    wsdev->pool_idx = pool_idx;
    wsdev->gpio = gpio;
    wsdev->num_leds = num_leds;
    wsdev->brightness = (uint8_t)brightness;
    if (ws2812_parse_color_order(color_order, wsdev->color_order) != VFS_OK)
        goto err_pool;
    wsdev->bytes_per_led = (size_t)bytes_per_led;
    wsdev->default_timeout_ms = (uint32_t)default_timeout_ms;
    wsdev->tx_buf = s_ws2812_tx_buf[pool_idx];
    wsdev->hw.gpio = gpio;
    wsdev->hw.rmt_resolution_hz = (uint32_t)rmt_resolution_hz;
    wsdev->hw.rmt_mem_block = (uint32_t)rmt_mem_block;
    wsdev->hw.rmt_queue_depth = (uint32_t)rmt_queue_depth;
    wsdev->hw.t0h_ticks = (uint32_t)t0h_ticks;
    wsdev->hw.t0l_ticks = (uint32_t)t0l_ticks;
    wsdev->hw.t1h_ticks = (uint32_t)t1h_ticks;
    wsdev->hw.t1l_ticks = (uint32_t)t1l_ticks;
    wsdev->hw.reset_ticks = (uint32_t)reset_ticks;

    if (osal_mutex_create_static(&wsdev->io_mutex, s_ws2812_mutex_storage[pool_idx],
                                 sizeof(s_ws2812_mutex_storage[pool_idx])) != 0)
        goto err_pool;

    device_lc_bind(dev, wsdev->io_mutex);

    ws2812_clear_frame(wsdev);
    if (hal_pulse_ws2812_open(wsdev->engine_id, &wsdev->hw) != 0)
        goto err_mutex;

    if (device_set_priv(dev, wsdev) != VFS_OK)
        goto err_mutex;
    dev->ops = &ws2812_fops;
    DRV_LOGI(kTag, "probe OK: gpio=%d, num_leds=%d", wsdev->gpio, wsdev->num_leds);
    return VFS_OK;

err_mutex:
    osal_mutex_destroy(wsdev->io_mutex);
    wsdev->io_mutex = NULL;
err_pool:
    __builtin_memset(wsdev, 0, sizeof(*wsdev));
    osal_pool_release(&s_ws2812_pool_ctrl, pool_idx);
    return VFS_ERR_IO;
}

static int ws2812_remove(struct device* dev)
{
    if (!dev)
        return VFS_ERR_INVAL;

    struct ws2812_device* wsdev = ws2812_get_drvdata(dev);
    if (IS_ERR(wsdev))
        return PTR_ERR(wsdev);

    struct osal_mutex* io_mutex = wsdev->io_mutex;
    int engine_id = wsdev->engine_id;
    int pool_idx = wsdev->pool_idx;

    struct dev_lifecycle* lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    dev_lc_remove_start(lc);
    device_ops_unregister(dev);

    int ret = dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER);
    /*
     * Debug: assert 快速暴露 drain 不变量违反.
     * Release: 保守降级 — 记录错误并保留 pool 槽 (避免 UAF), 不 assert.
     */
#ifndef NDEBUG
    assert(ret == VFS_OK);
#else
    if (ret != VFS_OK)
    {
        DRV_LOGE(kTag, "remove drain failed ret=%d opens=%d io_active=%d — "
                 "leaving pool slot allocated",
                 ret, dev_lc_opens(lc), dev_lc_io_active_count(lc));
        return ret;
    }
#endif

    /* drain/finish 持锁契约见 dev_lifecycle.h; HAL send 为同步 rmt_tx_wait_all_done */
    ws2812_clear_frame(wsdev);
    (void)ws2812_push_frame(wsdev, wsdev->default_timeout_ms);

    hal_pulse_ws2812_close(engine_id);
    osal_mutex_destroy(io_mutex);
    dev_lc_remove_finish(lc);
    osal_pool_release(&s_ws2812_pool_ctrl, pool_idx);
    __builtin_memset(wsdev, 0, sizeof(*wsdev));
    return VFS_OK;
}

DRIVER_REGISTER(ws2812, "esp32,ws2812", ws2812_probe, ws2812_remove)


