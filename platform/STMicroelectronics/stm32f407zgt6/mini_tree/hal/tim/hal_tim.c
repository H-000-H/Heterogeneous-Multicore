/*
 * TIM HAL — STM32 LL 定时器平台实现
 *
 * hw_instance 携带 DTSI 外设基地址, 直接强转取 TIM_TypeDef
 * host 池大小由 DTC_GEN_STM32_TIM_HOST_MAX 决定, priv 内嵌同步信号量
 */
#include "hal_tim.h"
#include "stm32f4xx_ll_tim.h"
#include "stm32f4xx.h"
#include "dt_config_gen.h"
#include "osal.h"

#include "VFS.h"
#ifndef DTC_GEN_STM32_TIM_HOST_MAX
#define DTC_GEN_STM32_TIM_HOST_MAX  14
#endif


#define HAL_TIM_HOST_MAX  DTC_GEN_STM32_TIM_HOST_MAX
struct hal_tim_stm32_priv
{
    TIM_TypeDef*     TIMx;
    struct osal_sem* sync_sem;
    int              hw_idx;   /* host_id, 索引 per-host dummy buffer */
};

_Static_assert(sizeof(struct hal_tim_stm32_priv) <= HAL_TIM_HW_PRIV_SIZE,"hal_tim_stm32_priv exceeds HAL_TIM_HW_PRIV_SIZE");

static struct hal_tim_host s_tim_hosts[HAL_TIM_HOST_MAX];

/* hw_instance 携带外设基地址 (来自 DTSI hw-instance), 直接强转取结构体 */
static TIM_TypeDef* stm32_tim_instance(int hw_instance)
{
    if (hw_instance <= 0)
        return NULL;
    return (TIM_TypeDef*)(uintptr_t)hw_instance;
}

static struct hal_tim_stm32_priv* stm32_priv(struct hal_tim_host* host)
{
    return (struct hal_tim_stm32_priv*)host->hw_priv_storage;
}

int hal_tim_host_init(int host_id, const struct hal_tim_device_config *cfg,const struct hal_tim_channel_config*ch_cfg)
{
    struct hal_tim_stm32_priv*  priv;
    struct hal_tim_host*        host;
    TIM_TypeDef*                tim;
    if(host_id < 0 || host_id >= HAL_TIM_HOST_MAX || !cfg)
        return VFS_ERR_INVAL;

    if(cfg->tim_id != host_id)
        return VFS_ERR_INVAL;

    host = &s_tim_hosts[host_id];

    tim = stm32_tim_instance(cfg->hw_instance);
    if(!tim)
        return VFS_ERR_NODEV;

    __builtin_memset(host,0,sizeof(*host));
    host ->cfg = *cfg;
    /* counter_mode / one_pulse_mode 直接使用平台原生值, 不做 HAL 层映射 */
    if(host->cfg.freq_hz == 0 || host->cfg.period_us == 0 ||
       host->cfg.dead_time_ns > 1000000)
        return VFS_ERR_INVAL;
    host->channel_cfg = *ch_cfg;
    if(host->channel_cfg.channel < 0 || host->channel_cfg.channel > 4)
        return VFS_ERR_INVAL;
    priv = stm32_priv(host);
    __builtin_memset(priv,0,sizeof(*priv));
    priv->TIMx   = tim;
    priv->hw_idx =host_id;
    host->host_ready = true;
    return VFS_OK;
}

int hal_tim_host_deinit(int host_id)
{
    struct hal_tim_host*host;
    if(host_id<0||host_id>HAL_TIM_HOST_MAX)
        return VFS_ERR_INVAL;

    host = &s_tim_hosts[host_id];
    if(!host->host_ready)
        return VFS_OK;

    if(host->ref_cnt>0)
        return VFS_ERR_BUSY;

    host->host_ready = 0;
    return VFS_OK;
}

int hal_tim_host_get(int host_id,struct hal_tim_host**out)
{
    struct hal_tim_host*host;

    if(!out)
        return VFS_ERR_INVAL;

    *out =NULL;

    if(host_id<0||host_id>=HAL_TIM_HOST_MAX)
        return VFS_ERR_INVAL;

    host = &s_tim_hosts[host_id];
    if(!host->host_ready)
        return VFS_ERR_NODEV;

    *out = host;
    return VFS_OK;
}