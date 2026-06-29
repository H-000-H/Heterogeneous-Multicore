/*
 * SPI RX Task — 收包 → SystemCmd 分发
 *
 * 唯一职责：阻塞等 SPI master 发数据，解析命令名 + 参数，
 * 调用 SystemCmd::dispatch() 路由到 handler。
 *
 * Handler 只做"邮局"投递，不阻塞，不影响收包循环。
 */

#include "app_spi_task.hpp"

#include "device.h"
#include "VFS.h"
#include "system_cmd.hpp"
#include "system_wdt.h"
#include "osal.h"
#include "task_manager.h"
#include "system_log.h"

#include <cstring>

static constexpr const char* kTag = "SpiTask";

static void spi_rx_task_entry(void* arg)
{
    struct device* dev = static_cast<struct device*>(arg);
    uint8_t buf[128];

    for (;;)
    {
        system_wdt_feed();

        int len = device_read(dev, buf, sizeof(buf), OSAL_WAIT_FOREVER);
        if (len <= 0)
            continue;

        // 包格式: 命令名(null结尾) + 参数字节
        const char* cmd_name = reinterpret_cast<const char*>(buf);
        size_t name_len = std::strlen(cmd_name) + 1;
        if (name_len >= sizeof(buf))
        {
            SYS_LOGW(kTag, "malformed packet");
            continue;
        }

        const void* cmd_args = buf + name_len;
        size_t args_len = static_cast<size_t>(len) - name_len;

        // 路由到 handler — 不传 dev 指针, handler 只做投递
        SystemCmd::getInstance().dispatch(cmd_name, cmd_args, args_len, nullptr);
    }
}

void app_spi_task_start(void)
{
    struct device* dev = device_find_by_label("fft_slave");
    if (IS_ERR(dev))
    {
        SYS_LOGW(kTag, "fft_slave not found");
        return;
    }

    if (device_open(dev, nullptr) != VFS_OK)
    {
        SYS_LOGE(kTag, "device_open failed");
        return;
    }

    task_manager_create_task("spi_rx", 1024, 9, spi_rx_task_entry, dev, 0);
    SYS_LOGI(kTag, "started, device=%s", device_get_name(dev));
}
