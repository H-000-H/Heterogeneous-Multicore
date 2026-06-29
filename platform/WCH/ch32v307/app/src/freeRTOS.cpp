#include "FreeRTOS.h"
#include "task.h"

#include "safe_state.h"
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    enter_safe_state("FreeRTOS stack overflow");
}

void task_rtos_main()
{
    vTaskStartScheduler();
}
