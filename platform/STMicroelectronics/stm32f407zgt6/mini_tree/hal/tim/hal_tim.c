/* SPDX-License-Identifier: Apache-2.0 */
/*
 * 该文件实现了 TIM 和 PWM 的 HAL 接口
 * 实现了 TIM 和 PWM 的初始化、设置、读取、关闭等基本功能
 */
#include "hal_tim.h"
#include "stm32f4xx_ll_tim.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"

#ifndef HAL_TIM_NUM
#define HAL_TIM_NUM 2U
#endif

/*===========================================================================================================================================================*/
/*结构体和全局变量定义*/
/*===========================================================================================================================================================*/
#define HAL_TIM_MODE_BASE 0
#define HAL_TIM_MODE_OC             (HAL_TIM_MODE_BASE + 1)
#define HAL_TIM_MODE_IC             (HAL_TIM_MODE_BASE + 2)
#define HAL_TIM_MODE_ENCODER        (HAL_TIM_MODE_BASE + 3)
#define HAL_TIM_MODE_BDTR           (HAL_TIM_MODE_BASE + 4)
#define HAL_TIM_MODE_HALLSENSOR     (HAL_TIM_MODE_BASE + 5)
#define TIM_DRIVER_COUNT            (HAL_TIM_MODE_BASE + 6)

struct hal_platform_unique_cfg
{
    uint8_t         index;
    uintptr_t       tim_handle;
    uint16_t        mode;
    uintptr_t       private_cfg;
};



typedef struct tim_driver
{
    int (*init)(void* tim_handle, void* cfg_ptr, hal_tim_device* pdev);
    int (*close)(void* tim_handle, hal_tim_device* pdev);
} tim_driver_t;

//TODO gpio子系统设计问题这东西不应该在这里需要修改gpio子系统
static int hal_tim_config_af_pin(const hal_tim_pin_config* pin)
{
    GPIO_TypeDef* port;

    if (!pin || !pin->set_af_func)
        return VFS_ERR_INVAL;

    port = (GPIO_TypeDef*)pin->port;
    
    LL_AHB1_GRP1_EnableClock(pin->clk_bus);
    
    LL_GPIO_SetPinMode(port, pin->pin, LL_GPIO_MODE_ALTERNATE);
    
    pin->set_af_func(port, pin->pin, pin->af);
    
    LL_GPIO_SetPinOutputType(port, pin->pin, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(port, pin->pin, LL_GPIO_SPEED_FREQ_HIGH);
    
    return VFS_OK;
}
static int _init_encoder(void* tim_handle, void* cfg_ptr,hal_tim_device* pdev) 
{
    if(!pdev||!tim_handle||!cfg_ptr)
        return VFS_ERR_INVAL;  
    hal_tim_config_af_pin(&pdev->host->pin); 
    LL_TIM_ENCODER_InitTypeDef* ENCODER_HANDLE    =    (LL_TIM_ENCODER_InitTypeDef*)cfg_ptr;
    ENCODER_HANDLE->EncoderMode     = pdev->host->encoder.hw_cfg.mode;
    ENCODER_HANDLE->IC1ActiveInput  = pdev->host->encoder.hw_cfg.ic1_active_input;
    ENCODER_HANDLE->IC1Filter       = pdev->host->encoder.hw_cfg.ic1_filter;
    ENCODER_HANDLE->IC1Prescaler    = pdev->host->encoder.hw_cfg.ic1_prescaler;
    ENCODER_HANDLE->IC1Polarity     = pdev->host->encoder.hw_cfg.ic1_polarity;

    ENCODER_HANDLE->IC2ActiveInput  = pdev->host->encoder.hw_cfg.ic2_active_input;
    ENCODER_HANDLE->IC2Filter       = pdev->host->encoder.hw_cfg.ic2_filter;
    ENCODER_HANDLE->IC2Prescaler    = pdev->host->encoder.hw_cfg.ic2_prescaler;
    ENCODER_HANDLE->IC2Polarity     = pdev->host->encoder.hw_cfg.ic1_polarity;
    LL_TIM_ENCODER_StructInit(ENCODER_HANDLE);
    if(LL_TIM_ENCODER_Init((TIM_TypeDef*)tim_handle,ENCODER_HANDLE)!= SUCCESS)
        return VFS_ERR_NODEV;
    return VFS_OK; 
}
static int _init_oc(void* tim_handle, void* cfg_ptr,hal_tim_device* pdev)   
{ 
    if(!pdev||!tim_handle||!cfg_ptr)
        return VFS_ERR_INVAL;
    hal_tim_config_af_pin(&pdev->host->pin); 
    LL_TIM_OC_InitTypeDef * OC_INIT_HANDLE  = (LL_TIM_OC_InitTypeDef*)cfg_ptr;
    OC_INIT_HANDLE->CompareValue            = pdev->host->output_compare.compare_value;
    OC_INIT_HANDLE->OCMode                  = pdev->host->output_compare.oc_mode;
    OC_INIT_HANDLE->OCIdleState             = pdev->host->output_compare.oc_n_idle_state;
    OC_INIT_HANDLE->OCNIdleState            = pdev->host->output_compare.oc_n_idle_state;
    OC_INIT_HANDLE->OCIdleState             = pdev->host->output_compare.oc_idle_state;
    OC_INIT_HANDLE->OCNPolarity             = pdev->host->output_compare.oc_polarity;
    OC_INIT_HANDLE->OCNState                = pdev->host->output_compare.oc_n_state;
    OC_INIT_HANDLE->OCState                 = pdev->host->output_compare.oc_state;
    LL_TIM_OC_StructInit(OC_INIT_HANDLE);
    if(LL_TIM_OC_Init((TIM_TypeDef*)tim_handle, pdev->host->channel.channel_id, OC_INIT_HANDLE)!=SUCCESS)
        return VFS_ERR_NODEV;
    return VFS_OK; 
}
static int _init_ic(void* tim_handle, void* cfg_ptr,hal_tim_device* pdev)   
{ 
    if(!pdev||!tim_handle||!cfg_ptr)
        return VFS_ERR_INVAL;
    hal_tim_config_af_pin(&pdev->host->pin); 
    LL_TIM_IC_InitTypeDef* IC_INIT_HANDLE   = (LL_TIM_IC_InitTypeDef*)cfg_ptr;
    IC_INIT_HANDLE->ICActiveInput           = pdev->host->input_capture.active_input;
    IC_INIT_HANDLE->ICFilter                = pdev->host->input_capture.filter;
    IC_INIT_HANDLE->ICPolarity              = pdev->host->input_capture.polarity;
    IC_INIT_HANDLE->ICPrescaler             = pdev->host->input_capture.prescaler;
    LL_TIM_IC_StructInit(IC_INIT_HANDLE);
    if(LL_TIM_IC_Init((TIM_TypeDef*)tim_handle, pdev->host->channel.channel_id,IC_INIT_HANDLE)!=SUCCESS)
        return VFS_ERR_NODEV; 
    return VFS_OK;
}
static int _init_bdtr(void* tim_handle, void* cfg_ptr,hal_tim_device* pdev) 
{ 
    if (!pdev||!tim_handle||!cfg_ptr) 
        return VFS_ERR_INVAL;
    hal_tim_config_af_pin(&pdev->host->pin); 
    LL_TIM_BDTR_InitTypeDef* BDTR_INIT_HANDLE   = (LL_TIM_BDTR_InitTypeDef*)cfg_ptr;
    BDTR_INIT_HANDLE->AutomaticOutput           = pdev->host->bdtr.automatic_output;
    BDTR_INIT_HANDLE->BreakPolarity             = pdev->host->bdtr.break_polarity;
    BDTR_INIT_HANDLE->BreakState                = pdev->host->bdtr.break_state;
    BDTR_INIT_HANDLE->OSSIState                 = pdev->host->bdtr.ossi_state;
    BDTR_INIT_HANDLE->OSSRState                 = pdev->host->bdtr.ossr_state;
    BDTR_INIT_HANDLE->DeadTime                  = pdev->host->bdtr.dead_time;
    BDTR_INIT_HANDLE->LockLevel                 = pdev->host->bdtr.lock_level;
    LL_TIM_BDTR_StructInit(BDTR_INIT_HANDLE);
    if(LL_TIM_BDTR_Init((TIM_TypeDef*)tim_handle,BDTR_INIT_HANDLE)!=SUCCESS)
        return VFS_ERR_NODEV; 
    return VFS_OK; 
}

static int _init_hall(void* tim_handle, void* cfg_ptr,hal_tim_device* pdev)
{
    if (!pdev||!tim_handle||!cfg_ptr) 
        return VFS_ERR_INVAL;
    hal_tim_config_af_pin(&pdev->host->pin); //TODO先在这里标个带点等一下条件判断gpio复用
    LL_TIM_HALLSENSOR_InitTypeDef* HALL_INIT_HANDLE = (LL_TIM_HALLSENSOR_InitTypeDef*)cfg_ptr;
    HALL_INIT_HANDLE->CommutationDelay              = pdev->host->hall.hall_commutation_delay_time;
    HALL_INIT_HANDLE->IC1Filter                     = pdev->host->hall.hall_filter_time;
    HALL_INIT_HANDLE->IC1Polarity                   = pdev->host->hall.hall_polarity;
    HALL_INIT_HANDLE->IC1Prescaler                  = pdev->host->hall.hall_prescaler;
    LL_TIM_HALLSENSOR_StructInit(HALL_INIT_HANDLE);
    LL_TIM_HALLSENSOR_Init(tim_handle,HALL_INIT_HANDLE);
    return VFS_OK;
}

static int _close_encode(void* tim_handle, struct hal_tim_device* pdev)
{
    if (!pdev || !tim_handle) 
        return VFS_ERR_INVAL;
    TIM_TypeDef* TIMx = (TIM_TypeDef*)tim_handle;
    LL_TIM_SetSlaveMode(TIMx, LL_TIM_SLAVEMODE_DISABLED);
    LL_TIM_CC_DisableChannel(TIMx, LL_TIM_CHANNEL_CH1);
    LL_TIM_CC_DisableChannel(TIMx, LL_TIM_CHANNEL_CH2);
    LL_TIM_DisableCounter(TIMx);

    return VFS_OK;
}

static int _close_oc(void* tim_handle, struct hal_tim_device* pdev)
{
    if (!pdev || !tim_handle) 
        return VFS_ERR_INVAL;
    TIM_TypeDef* TIM_Handle = (TIM_TypeDef*)tim_handle;
    LL_TIM_CC_DisableChannel(TIM_Handle, pdev->host->channel.channel_id);
    LL_TIM_OC_SetMode(TIM_Handle, pdev->host->channel.channel_id, LL_TIM_OCMODE_FORCED_INACTIVE);
    return VFS_OK;
}

static int _close_ic(void* tim_handle, struct hal_tim_device* pdev)
{
    if(!tim_handle||!pdev)
        return VFS_ERR_INVAL;
    TIM_TypeDef* TIM_Handle = (TIM_TypeDef*)tim_handle;
    LL_TIM_CC_DisableChannel(TIM_Handle,pdev->host->channel.channel_id);
    return VFS_OK;
}

static int _close_bdtr(void* tim_handle, struct hal_tim_device* pdev)
{
    if(!tim_handle||!pdev)
        return VFS_ERR_INVAL;
    TIM_TypeDef* TIM_Handle = (TIM_TypeDef*)tim_handle;
    LL_TIM_CC_DisableChannel(TIM_Handle, pdev->host->channel.channel_id);
    LL_TIM_OC_SetMode(TIM_Handle, pdev->host->channel.channel_id, LL_TIM_OCMODE_FORCED_INACTIVE);
    return VFS_OK;
}

static int _close_hall(void* tim_handle, struct hal_tim_device* pdev)
{
    return VFS_OK;
}

static const tim_driver_t tim_drivers[TIM_DRIVER_COUNT] = 
{
    [HAL_TIM_MODE_OC]           = { _init_oc        ,       _close_oc       },
    [HAL_TIM_MODE_IC]           = { _init_ic        ,       _close_ic       },
    [HAL_TIM_MODE_ENCODER]      = { _init_encoder   ,       _close_encode   },
    [HAL_TIM_MODE_BDTR]         = { _init_bdtr      ,       _close_bdtr     },
    [HAL_TIM_MODE_HALLSENSOR]   = { _init_hall      ,       _close_hall     },
};
/*===========================================================================================================================================================*/
/*函数声明*/
/*===========================================================================================================================================================*/

int hal_tim_device_init(hal_tim_device* pdev, hal_platform_unique_config* unique_cfg,hal_tim_host_config* host)
{
    if (!pdev || !unique_cfg)
        return VFS_ERR_INVAL;
    COMPAT_MEM_SET(pdev, 0, sizeof(*pdev));
    pdev->unique = unique_cfg;
    pdev->host   = host;
    return VFS_OK;
}

int hal_tim_device_deinit(hal_tim_device* pdev)
{
    if (!pdev)
        return VFS_OK;
    if (pdev->unique)
        COMPAT_MEM_SET(pdev->unique, 0, sizeof(struct hal_platform_unique_cfg));
    if (pdev->host)
        COMPAT_MEM_SET(pdev->host, 0, sizeof(hal_tim_host_config));
    pdev->unique = NULL;
    pdev->host   = NULL;
    return VFS_OK;
}

int hal_tim_open(hal_tim_device *pdev)
{
    TIM_TypeDef* tim_handle;
    LL_TIM_InitTypeDef TIM_InitStruct={0};
    
    if (!pdev || !pdev->unique)
        return VFS_ERR_INVAL;

    tim_handle = (TIM_TypeDef*)pdev->unique->tim_handle;
    if(!tim_handle)
        return VFS_ERR_INVAL;
    hal_tim_config_af_pin(&pdev->host->pin);
    TIM_InitStruct.Autoreload          =       pdev->host->base.autoreload;
    TIM_InitStruct.ClockDivision       =       pdev->host->base.clock_division;
    TIM_InitStruct.CounterMode         =       pdev->host->base.counter_mode;
    TIM_InitStruct.RepetitionCounter   =       pdev->host->base.repetition_counter;
    TIM_InitStruct.Prescaler           =       pdev->host->base.prescaler;
    if(LL_TIM_Init(tim_handle,&TIM_InitStruct)!=SUCCESS)
        return VFS_ERR_NODEV;

    uint32_t mode = pdev->unique->mode;

    if (mode >= TIM_DRIVER_COUNT || !tim_drivers[mode].init)
        return VFS_ERR_INVAL;

    pdev->init_func  = tim_drivers[mode].init;
    pdev->close_func = tim_drivers[mode].close;

    if (pdev->init_func(tim_handle, (void*)pdev->unique->private_cfg, pdev) != VFS_OK)
        return VFS_ERR_NODEV;

    return VFS_OK;
}

int hal_tim_close(hal_tim_device *pdev)
{
    if (!pdev || !pdev->unique || !pdev->close_func)
        return VFS_ERR_INVAL;
    return pdev->close_func((TIM_TypeDef*)pdev->unique->tim_handle, pdev);
}
