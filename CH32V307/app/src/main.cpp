#include "freeRTOS.hpp"

extern "C" __attribute__((used, section(".entry"))) int ch307_node_main(void)
{
    task_rtos_main();
    return 0;
}