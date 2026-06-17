/*
 * hal_esp32s3.c — ESP32-S3 HAL 实现
 *
 * 将 mini_tree 的 hal_if 接口映射到 ESP-IDF 驱动层.
 * 安全停机函数置于 IRAM 以确保在 cache 禁用后仍可执行.
 */
#include "hal_gpio.h"
#include "hal_pwm.h"
#include "hal_cpu.h"
#include "hal_storage.h"
#include "hal_flash.h"
#include "hal_wdt.h"
#include "hal_platform_safety.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stddef.h>
#include <string.h>

/* ── GPIO ── */
int hal_gpio_set_level(hal_pin_t pin, int level)
{
    int num = HAL_PIN_NUM(pin);
    gpio_set_level((gpio_num_t)num, level);
    return 0;
}

int hal_gpio_get_level(hal_pin_t pin)
{
    int num = HAL_PIN_NUM(pin);
    return gpio_get_level((gpio_num_t)num);
}

/* ── PWM 强制停止 (stub: 安全状态机占位) ── */
void hal_pwm_force_stop_all(void)
{
    /* ESP32-S3 无统一 PWM 紧急停止 — 由各驱动自己的 remove 回调负责 */
}

void hal_pwm_init_struct(struct hal_pwm_channel* pwm)
{
    if (pwm) {
        pwm->init     = NULL;
        pwm->set_duty = NULL;
        pwm->get_duty = NULL;
        pwm->deinit   = NULL;
        pwm->_impl    = NULL;
    }
}

/* ── CPU 紧急停机 (IRAM 驻留) ── */
IRAM_ATTR void hal_cpu_emergency_stop_all_cores(void)
{
    /* 禁用全局中断后死锁 */
    __asm__ volatile("rsil a2, 5" ::: "a2", "memory");
    while (1) {
        asm volatile("waiti 0" ::: "memory");
    }
}

/* ── Storage stubs (最小构建, 无持久化) ── */
bool hal_storage_init(void)           { return false; }
bool hal_storage_erase_all(void)       { return false; }
bool hal_storage_read_flag(uint8_t* f) { (void)f; return false; }
bool hal_storage_write_flag(uint8_t f) { (void)f; return false; }
bool hal_storage_read_blob(uint8_t slot, uint8_t* buf, size_t* len)
{ (void)slot; (void)buf; (void)len; return false; }
bool hal_storage_write_blob(uint8_t slot, const uint8_t* buf, size_t len)
{ (void)slot; (void)buf; (void)len; return false; }

/* ── Flash stub (最小构建, 无巡检) ── */
bool hal_flash_read(uint32_t addr, uint8_t* buf, size_t len)
{ (void)addr; (void)buf; (void)len; return false; }
uint32_t hal_flash_get_app_addr(void) { return 0; }
uint32_t hal_flash_get_app_size(void) { return 0; }

/* ── WDT ── */
bool hal_wdt_init_rtc(uint32_t timeout_ms) { (void)timeout_ms; return false; }
void hal_wdt_feed_rtc(void) {}
void hal_wdt_rtc_set_long_timeout(void) {}
void hal_wdt_rtc_restore_timeout(void) {}

static bool s_twdt_inited = false;

bool hal_wdt_init_twdt(uint32_t timeout_ms)
{
    if (s_twdt_inited) return true;

    esp_task_wdt_config_t cfg = {
        .timeout_ms     = timeout_ms,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    esp_err_t err = esp_task_wdt_init(&cfg);
    if (err == ESP_ERR_INVALID_STATE) {
        s_twdt_inited = true;
        return true;
    }
    if (err != ESP_OK) return false;

    s_twdt_inited = true;
    return true;
}

bool hal_wdt_subscribe(void* task_handle)
{
    if (!s_twdt_inited) return false;
    return esp_task_wdt_add((TaskHandle_t)task_handle) == ESP_OK;
}

bool hal_wdt_unsubscribe(void* task_handle)
{
    if (!s_twdt_inited) return false;
    return esp_task_wdt_delete((TaskHandle_t)task_handle) == ESP_OK;
}

void hal_wdt_feed_twdt(void)
{
    if (!s_twdt_inited) return;
    esp_task_wdt_reset();
}

/* ── Platform safety stubs (最小构建) ── */
void hal_platform_critical_hardware_lock(void)
{
    hal_pwm_force_stop_all();
}

void hal_platform_nmi_emergency_stamp(void)
{
    /* NMI 紧急标记: 最小构建无持久化, 仅作占位 */
}