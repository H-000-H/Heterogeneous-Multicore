#include "app_rtos.hpp"

extern "C" __attribute__((used)) void app_main(void)
{
    app_rtos_start();
}
