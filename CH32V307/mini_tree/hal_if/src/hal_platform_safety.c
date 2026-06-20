#include "hal_platform_safety.h"
#include "hal_pwm.h"
#include "compiler_compat_poison.h"

void hal_platform_critical_hardware_lock(void)
{
    hal_pwm_force_stop_all();
}

void hal_platform_nmi_emergency_stamp(void)
{
    /* CH32V307: 最小桩实现, 后续可写入 BKP/RTC 备份寄存器 */
}
