/*
 * Business Command Handlers — 异步"邮局"模式
 *
 * Handler 不碰硬件, 不阻塞, 只做一件事:
 *   将收到的强类型命令压入领域任务的专属队列.
 *
 * 真正的硬件操作在各领域任务 (led_task / flash_task) 中执行.
 */

#include "app_cmd_handlers.hpp"

#include "system_cmd.hpp"
#include "system_log.h"

static constexpr const char* kTag = "AppCmd";

/* ── LED: 投递到 led_task 队列 ── */
static bool handleLedSet(const CmdLedSet& cmd, void* /*ctx*/)
{
    if (osal_queue_send(g_led_queue, &cmd, 0) != true)
    {
        SYS_LOGW(kTag, "Led queue full — command dropped");
        return false;
    }
    return true;
}

/* ── Flash: 投递到 flash_task 队列 ── */
static bool handleFlashErase(const CmdFlashErase& cmd, void* /*ctx*/)
{
    if (osal_queue_send(g_flash_queue, &cmd, 0) != true)
    {
        SYS_LOGW(kTag, "Flash queue full — command dropped");
        return false;
    }
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  注册所有业务命令
 * ═══════════════════════════════════════════════════════════════════════ */
void app_cmd_handlers_register(void)
{
    SystemCmd& sys = SystemCmd::getInstance();

    sys.registerCmd<CmdLedSet>("led_set", handleLedSet);
    sys.registerCmd<CmdFlashErase>("flash_erase", handleFlashErase);

    SYS_LOGI(kTag, "async handlers registered");
}
