/*
 * hal_pulse_engine_esp32s3.c - ESP32-S3 RMT pulse engine
 *
 * This is the platform side of the mini_tree pulse-engine HAL.  The WS2812
 * driver owns policy and device-tree parsing; this file only maps the prepared
 * timing parameters to ESP-IDF RMT resources.
 */
#include "hal_pulse_engine.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/gpio.h"
#include "compiler_compat_poison.h"

struct pulse_engine
{
    bool                 used;
    gpio_num_t           gpio;
    rmt_channel_handle_t chan;
    rmt_encoder_handle_t bytes_enc;
    rmt_encoder_handle_t copy_enc;
    rmt_symbol_word_t    reset_code;
};

static struct pulse_engine s_engines[HAL_PULSE_ENGINE_MAX];

static struct pulse_engine* pulse_engine_get(int id)
{
    if (id < 0 || id >= HAL_PULSE_ENGINE_MAX)
        return NULL;

    return &s_engines[id];
}

static int pulse_engine_hw_init(struct pulse_engine* engine,
                                const struct hal_pulse_ws2812_hw* hw)
{
    rmt_tx_channel_config_t tx_cfg =
    {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .gpio_num          = (gpio_num_t)hw->gpio,
        .mem_block_symbols = hw->rmt_mem_block,
        .resolution_hz     = hw->rmt_resolution_hz,
        .trans_queue_depth = hw->rmt_queue_depth,
        .flags.invert_out  = false,
        .flags.with_dma    = false,
    };

    if (rmt_new_tx_channel(&tx_cfg, &engine->chan) != ESP_OK)
        return -1;

    rmt_bytes_encoder_config_t byte_cfg =
    {
        .bit0 =
        {
            .duration0 = hw->t0h_ticks,
            .level0    = 1,
            .duration1 = hw->t0l_ticks,
            .level1    = 0,
        },
        .bit1 =
        {
            .duration0 = hw->t1h_ticks,
            .level0    = 1,
            .duration1 = hw->t1l_ticks,
            .level1    = 0,
        },
        .flags.msb_first = 1,
    };
    if (rmt_new_bytes_encoder(&byte_cfg, &engine->bytes_enc) != ESP_OK)
        goto err_del_chan;

    engine->reset_code = (rmt_symbol_word_t){
        .duration0 = hw->reset_ticks,
        .level0    = 0,
        .duration1 = 0,
        .level1    = 0,
    };

    rmt_copy_encoder_config_t copy_cfg = {};
    if (rmt_new_copy_encoder(&copy_cfg, &engine->copy_enc) != ESP_OK)
        goto err_del_bytes;

    if (rmt_enable(engine->chan) != ESP_OK)
        goto err_del_copy;

    engine->gpio = (gpio_num_t)hw->gpio;
    engine->used = true;
    return 0;

err_del_copy:
    rmt_del_encoder(engine->copy_enc);
err_del_bytes:
    rmt_del_encoder(engine->bytes_enc);
err_del_chan:
    rmt_del_channel(engine->chan);
    __builtin_memset(engine, 0, sizeof(*engine));
    return -1;
}

static void pulse_engine_hw_exit(struct pulse_engine* engine)
{
    if (!engine || !engine->used)
        return;

    rmt_disable(engine->chan);
    rmt_del_encoder(engine->copy_enc);
    rmt_del_encoder(engine->bytes_enc);
    rmt_del_channel(engine->chan);
    __builtin_memset(engine, 0, sizeof(*engine));
}

static int pulse_engine_xmit(struct pulse_engine* engine,
                             rmt_encoder_handle_t encoder,
                             const void* data, size_t len,
                             uint32_t timeout_ms)
{
    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };

    if (rmt_transmit(engine->chan, encoder, data, len, &tx_cfg) != ESP_OK)
        return -1;
    if (rmt_tx_wait_all_done(engine->chan, (int)timeout_ms) != ESP_OK)
        return -1;

    return 0;
}

int hal_pulse_ws2812_open(int engine_id, const struct hal_pulse_ws2812_hw* hw)
{
    struct pulse_engine* engine;

    if (!hw)
        return -1;

    engine = pulse_engine_get(engine_id);
    if (!engine || engine->used)
        return -1;

    return pulse_engine_hw_init(engine, hw);
}

int hal_pulse_ws2812_send(int engine_id, const uint8_t* data, size_t len,
                          uint32_t timeout_ms)
{
    struct pulse_engine* engine;

    if (!data || len == 0)
        return -1;

    engine = pulse_engine_get(engine_id);
    if (!engine || !engine->used)
        return -1;

    if (pulse_engine_xmit(engine, engine->bytes_enc, data, len, timeout_ms) != 0)
        return -1;
    if (pulse_engine_xmit(engine, engine->copy_enc, &engine->reset_code,
                          sizeof(engine->reset_code), timeout_ms) != 0)
        return -1;

    return 0;
}

void hal_pulse_ws2812_close(int engine_id)
{
    pulse_engine_hw_exit(pulse_engine_get(engine_id));
}

