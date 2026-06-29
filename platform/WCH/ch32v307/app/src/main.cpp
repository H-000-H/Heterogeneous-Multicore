#include "freeRTOS.hpp"
#include "system_init.h"
#include "driver.h"
#include "spi.h"
#include "usart.h"
#include "compiler_compat.h"

pre_execution(50)
static void ch32_board_periph_init(void)
{
    MX_SPI1_Init();
    MX_USART1_Init();
}

extern "C" __attribute__((used, section(".entry"))) int ch307_node_main(void){
    mini_tree_pre_os_init();
    board_register_all_drivers();
    mini_tree_start_tasks();
    system_init_complete();
    task_rtos_main();
    return 0;
}
