#pragma once

/*
 * Business Command Parameters & Async Handler Registration
 *
 * 所有 handler 只负责"邮局"投递——将强类型命令压入对应领域任务的专属队列,
 * 绝不阻塞 SPI RX 任务的收包循环.
 *
 * 各领域任务 (led_task / flash_task) 阻塞在自己的队列上执行真正的硬件操作.
 */

#include <cstdint>
#include <cstddef>
#include "osal.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  LED 命令
 * ═══════════════════════════════════════════════════════════════════════ */
struct CmdLedSet
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

/* ═══════════════════════════════════════════════════════════════════════
 *  Flash 命令
 * ═══════════════════════════════════════════════════════════════════════ */
struct CmdFlashErase
{
    uint32_t sector_addr;
};

/* ── 各个领域任务的专属队列 (在各任务 .cpp 中定义) ── */
extern osal_queue_handle_t g_led_queue;
extern osal_queue_handle_t g_flash_queue;

/* ── 注册所有业务命令到 SystemCmd ── */
void app_cmd_handlers_register(void);
