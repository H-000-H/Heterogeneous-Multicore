/*
 * TIM HAL — 通用定时器抽象 (平台原生模式直通)
 *
 * counter_mode / one_pulse_mode 直接使用 DTSI 平台原生值, 无 HAL 枚举映射
 * host/dev 二级管理: host_init/deinit/get + dev 池化打开/关闭
 */
#ifndef HAL_tim_H
#define HAL_tim_H
#include "compiler_compat.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifndef HAL_TIM_HW_PRIV_SIZE
#define HAL_TIM_HW_PRIV_SIZE  32
#endif
#ifdef __cplusplus
extern "C"
{
#endif

                                                            /*定时器模式枚举*/
/*===========================================================================================================================================================*/

/* 平台原生值直接由 DTSI 传入, 无 HAL 层枚举抽象。
 * STM32: LL_TIM_COUNTERMODE_* / LL_TIM_ONEPULSEMODE_*
 * ESP32: 0=up, 1=down
 * CH32:  与 STM32 一致
 * 平台不支持的模式由 hal_tim_host_init() 返回 VFS_ERR_NOTSUPP。
 */

/*===========================================================================================================================================================*/

                                                            /*定时器与通道配置*/
/*===========================================================================================================================================================*/
struct hal_tim_device_config
{
    int             tim_id;          /* 定时器编号, 如 0 = TIM1 */
    int             hw_instance;     /* 外设基地址 (来自 DTSI hw-instance) */
    uint32_t        freq_hz;         /* 定时器基础时钟(Hz), 用于计算分频 */
    uint32_t        counter_mode;    /* 计数方向, 平台原生值 (如 LL_TIM_COUNTERMODE_UP) */
    uint32_t        one_pulse_mode;  /* 单次/周期, 平台原生值 (如 LL_TIM_ONEPULSEMODE_*) */
    uint32_t        period_us;       /* 周期/超时(微秒) */
    uint32_t        dead_time_ns;    /* 死区时间(纳秒), 0 = 不使能 */
};

struct hal_tim_channel_config
{
    int channel;                    /* 通道号, 如 0 = CH1 */
    uint32_t mode;                  /* 通道工作模式 */
    uint32_t pulse;                 /* 脉冲宽度/比较值(与 period_us 同单位) */
    int polarity;                   /* 输出极性: 0 = 高有效, 1 = 低有效 */
};

struct hal_tim_host
{
    struct hal_tim_device_config        cfg;
    struct hal_tim_channel_config       channel_cfg;
    int                                 ref_cnt;
    int                                 hw_init;
    bool                                 host_ready;
    uint8_t                             hw_priv_storage[HAL_TIM_HW_PRIV_SIZE];
};

struct hal_tim_dev
{
    struct hal_tim_host*                host;
    struct hal_tim_channel_config       ch_cfg;
    struct hal_tim_device_config        cfg;
    int                                 pool_idx;
    int                                 hw_open;
};

/*===========================================================================================================================================================*/

                                                            /*定时器实体与 API*/
/*===========================================================================================================================================================*/
typedef void (*hal_tim_callback_t)(struct hal_tim_dev* dev, void* user_data);

int hal_tim_host_init(int host_id, const struct hal_tim_device_config *cfg,const struct hal_tim_channel_config*ch_cfg);
int hal_tim_host_deinit(int host_id);
int hal_tim_host_get(int host_id, struct hal_tim_host** out) COMPAT_WARN_UNUSED_RESULT;

/* ===== Device 管理 API (与 ESP32 对齐) ===== */
void hal_tim_dev_init(struct hal_tim_dev* dev, int pool_idx,
                      struct hal_tim_host* host,
                      const struct hal_tim_device_config* dev_cfg);
int hal_tim_dev_hw_open(struct hal_tim_dev* dev);
int hal_tim_dev_hw_close(struct hal_tim_dev* dev);


void hal_tim_init_struct(struct hal_tim_host* host);
/*===========================================================================================================================================================*/

                                                            /*安全停机*/
/*===========================================================================================================================================================*/
void hal_tim_force_stop(void);
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_tim_H */
