#ifndef HAL_PWM_H
#define HAL_PWM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" 
{
#endif

/* PWM 通道配置 */
struct hal_pwm_config
{
    int pin;                /* 输出引脚 */
    int freq_hz;            /* PWM 频率(Hz) */
    int resolution_bits;    /* 分辨率位数, 0 = 硬件默认 */
};

struct hal_pwm_channel
{
    int (*init)(struct hal_pwm_channel* pwm, const struct hal_pwm_config* cfg);
    int (*set_duty)(struct hal_pwm_channel* pwm, uint32_t duty);
    int (*get_duty)(struct hal_pwm_channel* pwm, uint32_t* duty);
    int (*deinit)(struct hal_pwm_channel* pwm);
    void* _impl;
};

void hal_pwm_init_struct(struct hal_pwm_channel* pwm);
void hal_pwm_force_stop_all(void);

/* ioctl 命令 */
#define PWM_CMD_SET_DUTY      0x01
#define PWM_CMD_GET_DUTY      0x02
#define PWM_CMD_SET_FREQ      0x03
#define PWM_CMD_DEINIT        0x04

#ifdef __cplusplus
}
#endif

#endif /* HAL_PWM_H */

