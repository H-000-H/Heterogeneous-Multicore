#ifndef HAL_tim_H
#define HAL_tim_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

                                                            /*定时器模式枚举*/
/*===========================================================================================================================================================*/
typedef enum
{
    HAL_tim_MODE_ONESHOT,     /* 单次模式 */
    HAL_tim_MODE_PERIODIC,    /* 周期模式 */
} hal_tim_mode_t;

typedef enum
{
    HAL_tim_CH_OUTPUT_COMPARE,    /* 输出比较 */
    HAL_tim_CH_PWM,               /* PWM 输出 */
    HAL_tim_CH_INPUT_CAPTURE,     /* 输入捕获 */
    HAL_tim_CH_ENCODER,           /* 编码器模式 */
} hal_tim_ch_mode_t;
/*===========================================================================================================================================================*/

                                                            /*定时器与通道配置*/
/*===========================================================================================================================================================*/
struct hal_tim_config
{
    int             tim_id;       /* 定时器编号, 如 0 = TIM1 */
    uint32_t        freq_hz;        /* 定时器基础时钟(Hz), 用于计算分频 */
    hal_tim_mode_t mode;          /* 单次/周期模式 */
    uint32_t        period_us;      /* 周期/超时(微秒) */
    uint32_t        dead_time_ns;   /* 死区时间(纳秒), 0 = 不使能 */
};

struct hal_tim_channel_config
{
    int channel;                    /* 通道号, 如 0 = CH1 */
    hal_tim_ch_mode_t mode;       /* 通道工作模式 */
    uint32_t pulse;                 /* 脉冲宽度/比较值(与 period_us 同单位) */
    int polarity;                   /* 输出极性: 0 = 高有效, 1 = 低有效 */
};
/*===========================================================================================================================================================*/

                                                            /*定时器实体与 API*/
/*===========================================================================================================================================================*/
struct hal_tim;
typedef void (*hal_tim_callback_t)(struct hal_tim* tim, void* user_data);

struct hal_tim
{
    int (*init)(struct hal_tim* tim, const struct hal_tim_config* cfg);
    int (*config_channel)(struct hal_tim* tim, const struct hal_tim_channel_config* ch_cfg);
    int (*start)(struct hal_tim* tim);
    int (*stop)(struct hal_tim* tim);
    int (*reset)(struct hal_tim* tim);
    int (*set_callback)(struct hal_tim* tim, hal_tim_callback_t cb, void* user_data);
    int (*get_counter)(struct hal_tim* tim, uint32_t* val);  /* 读取当前计数值 */
    int (*deinit)(struct hal_tim* tim);
    void* _impl;
};

void hal_tim_init_struct(struct hal_tim* tim);
/*===========================================================================================================================================================*/

                                                            /*安全停机*/
/*===========================================================================================================================================================*/
void hal_tim_force_stop(void);
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_tim_H */
