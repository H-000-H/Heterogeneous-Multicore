#include "app_freertos.hpp"
#include "app_cmd_handlers.hpp"
#include "app_spi_task.hpp"
#include "app_led_task.hpp"
#include "app_flash_task.hpp"

#include "system_init.h"
#include "driver.h"

#include "nvs_flash.h"
#include "esp_err.h"

extern "C" int app_rtos_start(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    mini_tree_pre_os_init();
    board_register_all_drivers();

    // 注册业务命令到 SystemCmd (驱动探测后、系统任务启动前)
    app_cmd_handlers_register();

    mini_tree_start_tasks();

    // 启动各领域任务
    app_spi_task_start();
    app_led_task_start();
    app_flash_task_start();

    system_init_complete();

    return 0;
}
