/**
 * @file        hal_tim.h
 * @brief       通用定时器硬件直投层(HAL)接口定义和tim用法扩展说明
 * @note        和定时器相关的配置和操作都放在hal_tim.h和hal_tim.c中,不区分PWM,输入捕获,输出比较,编码器,hall传感器
 * @note        不同模式是需要你去dtsi中自己去选择不同的模式,hal只是提供一个统一的接口让你去使用
 * @details     本文件定义了定时器在热路径(Hot-Path)下的高效率配置接口。
 * @note        所有接口设计为平台无关，由具体芯片平台(如 STM32, ESP32, CH307)进行底层硬实现。
 * @note        由于TIM是快速热路径外设所以TIM的初始化与配置应该尽量在硬件直投层完成
 * @note        vfs  层只负责拉取TIM的配置并传递给HAL层并且对hal层提供的api进行内联封装
 * @note        文件约定：返回值不允许void，必须使用int，并且错误码必须使用VFS.h中的错误码 
 * @note        获取参数不能直接返回，必须通过指针参数传递
 * @note        平台相关的不允许出现在hal.h中，必须出现在hal.c中
 */
#ifndef HAL_TIM_H
#define HAL_TIM_H

#include <stdint.h>
#include <stdbool.h>
#include "compiler_compat.h"
#include "VFS.h"
#ifdef __cplusplus
extern "C"
{
#endif

/*===========================================================================================================================================================*/

                                                            /*定时器与通道配置*/
/*===========================================================================================================================================================*/
/**
 * @brief TIM 引脚底层物理配置结构体
 * @note 用于跨平台 GPIO 复用映射，支持 STM32/ESP32/CH307 等物理引脚初始化
 */
struct hal_tim_pin_cfg
{
    uintptr_t port;        /**< GPIO 端口基地址或端口号 (支持 32/64 位平台指针转换) */
    uint16_t  pin;         /**< 引脚编号 (如 GPIO_PIN_0) */
    uint32_t  clk_bus;  /**< 该引脚所属的外设时钟总线 */
    uint32_t  af;          /**< 引脚复用功能设置 (Alternate Function 选择) */
    /**
     * @brief 设置引脚复用功能，因为不同平台设置引脚复用功能的方式不同，所以需要一个函数指针来实现
     * @note 设置引脚复用功能，用于设置引脚的复用功能,由具体平台实现
     * @param port 端口基地址
     * @param pin 引脚编号
     * @param af 引脚复用功能设置
     * @return 错误码
     */
    int (*set_af_func)(void *port, uint32_t pin, uint32_t af);
};

/**
 * @brief TIM 通道配置结构体
 * @note 用于配置定时器的通道
 */
struct hal_tim_chn_config_t
{
    uint32_t               channel_id; /**< 通道号：如 1..4 */
    uint32_t               mode;       // 当前通道的工作模式
    uint32_t               polarity;   /**< 极性 / 触发边沿 */
    uint32_t               filter;     /**< 滤波器配置（输入时去抖，0表示禁用） */
    uint32_t               prescaler;  /**< 通道分频（输入捕获时用） */
    uint32_t               enable_complementary; /**< 是否开启互补输出（高级定时器 CHxN） */
};

/**
 * @brief TIM 通道实体结构体
 * @note 用于绑定定时器的通道实体
 */
struct hal_tim_chn_t
{
    struct hal_tim_chn_config_t config; /**< 当前通道的配置快照 */
    void                *priv_data; /**< 指向底层硬件特定通道的指针（例如针对 STM32 指向某个特定寄存器偏移） */
    void (*callback)(void *arg); /**< 通道中断回调函数（如捕获完成、比较匹配） */
    void                *callback_arg; /**< 通道中断回调函数参数 */
};
 /**
  * @brief TIM 基础时基配置结构体
  * @note 属于核心热路径的基础时钟分频与计数范围定义
  */
struct hal_tim_base_cfg
{
    uint32_t  prescaler;          /**< 预分频器值 (PSC) */
    uint32_t  counter_mode;       /**< 计数模式。0: 向上计数, 1: 向下计数, 2: 中央对齐 */
    uint32_t  autoreload;         /**< 自动重装载值 (ARR) */
    uint32_t  clock_division;     /**< 时钟分频因子 (用于死区/滤波器采样) */
    uint32_t  repetition_counter; /**< 重复计数器值 (RCR，仅高级定时器有效) */
};
 
/**
 * @brief 硬件直投层 PWM / 输出比较通用配置结构体
 * @note 专用于定时器的输出通道配置（如电机驱动、LED 调光、蜂鸣器等向外打信号的场景）
 */
struct hal_output_compare_cfg
{
    uint32_t compare_value; /**< 比较值 */
    uint32_t oc_mode; /**< 输出比较模式 */
    uint32_t oc_state; /**< 输出比较状态 */
    uint32_t oc_polarity; /**< 输出比较极性 */
    uint32_t oc_idle_state; /**< 输出比较空闲状态 */
    uint32_t oc_n_state; /**< 输出比较互补状态 */
    uint32_t oc_n_polarity; /**< 输出比较互补极性 */
    uint32_t oc_n_idle_state; /**< 输出比较互补空闲状态 */
};

/**
 * @brief 硬件直投层 编码器硬件配置结构体
 * @note 专用于定时器的编码器模式配置
 */
 typedef struct hal_encoder_cfg 
{
    uint32_t mode;             /**< 计数模式：看 A 相、看 B 相，还是 A/B 双相都看（1/2/4倍频） */
    uint32_t period;           /**< 计数自动重装载值（ARR），通常设为线数*4 或 0xFFFF */
    
    // A相 (Channel 1) 配置
    uint32_t ic1_polarity;     /**< A相极性：不反相（上升沿）或 反相（下降沿） */
    uint32_t ic1_filter;       /**< A相滤波时间：去除硬件毛刺，防止误计数 */
    uint32_t ic1_active_input; /**< A相输入源 */
    uint32_t ic1_prescaler;    /**< A相分频器 */
    
    // B相 (Channel 2) 配置
    uint32_t ic2_polarity;     /**< B相极性：同上 */
    uint32_t ic2_filter;       /**< B相滤波时间 */
    uint32_t ic2_active_input; /**< B相输入源 */
    uint32_t ic2_prescaler;    /**< B相分频器 */
} hal_encoder_cfg;

/**
 * @brief 硬件直投层 编码器业务配置结构体
 * @note 专用于定时器的编码器模式配置
 */
typedef struct hal_encoder_config
{
    hal_encoder_cfg hw_cfg;     
    
    uint32_t pulse_per_rev;         /**< 编码器线数（如 500, 1024, 2500 PPR） */
    float    reduction_ratio;       /**< 减速比（如果电机后面接了减速箱） */
    
    int32_t  total_count;           /**< 软件累加的总脉冲数（考虑了正反转和溢出圈数） */
    int16_t  overflow_num;          /**< 溢出圈数 */
    float    current_velocity;      /**< 实时计算出来的转速 (RPM 或 rad/s) */
} hal_encoder_config;
/**
 * @brief 硬件直投层 输入捕获配置结构体
 * @note 专用于测量外部输入信号的频率、周期或脉冲宽度（向内拉信号的场景）
 */
struct hal_input_capture_cfg 
{
    uint32_t polarity; /**< 捕获触发边沿。0: 上升沿, 1: 下降沿, 2: 双边沿 */
    uint32_t filter;       /**< 输入数字滤波器配置。0x0: 不滤波, 0xF: 最大滤波，要求连续15次采样稳定 */
    uint32_t prescaler;    /**< 输入分频器。0: 每次边沿都触发捕获, 1: 每2次边沿触发一次, 2: 每4次... (用于降频测量高频信号) */
    uint32_t active_input;   /**< 输入源映射。0: 直连通道, 1: 交叉通道 */
};

struct hal_bdtr_cfg
{
    uint32_t automatic_output; /**< 自动输出模式。0: 不自动输出, 1: 自动输出 */
    uint32_t break_state; /**< 断路器状态。0: 不启用, 1: 启用 */
    uint32_t break_polarity; /**< 断路器极性。0: 低电平, 1: 高电平 */
    uint32_t break_filter; /**< 断路器滤波器。0x0: 不滤波, 0xF: 最大滤波，要求连续15次采样稳定 */
    uint32_t break_prescaler; /**< 断路器分频器。0: 每次边沿都触发捕获, 1: 每2次边沿触发一次, 2: 每4次... (用于降频测量高频信号) */
    uint32_t break_active_input; /**< 断路器输入源映射。0: 直连通道, 1: 交叉通道 */
    uint32_t ossi_state; /**< 空闲状态。0: 不启用, 1: 启用 */
    uint32_t ossr_state; /**< 运行状态。0: 不启用, 1: 启用 */
    uint32_t dead_time; /**< 死区时间。0x0: 不启用, 0xFF: 最大死区时间(单位: 时钟周期) */
    uint32_t lock_level; /**< 锁定级别。0: 不锁定, 1: 锁定, 2: 锁定, 3: 锁定 */
};

/**
 * @brief 通用硬件霍尔传感器配置结构体
 * @note 信号调理参数: filter 和 polarity 的数值含义由框架统一规定(如时间常数或统一抽象值)
 * @note 换向时序控制策略: 换向延时时间(单位: 时钟周期)
 * @note 若是无硬件滤波，则需要软件延时
 * @note 架构本身有dts净化,所以次处只是传递业务属性,不包含物理属性
 */
 struct hal_hall_cfg
 {
     uint32_t hall_polarity; /**< 霍尔传感器极性：不反相（上升沿）或 反相（下降沿） */
     uint32_t hall_filter_time;          /**< 霍尔信号去抖时间(单位: 时钟周期) */
     uint32_t hall_prescaler;            /**< 霍尔传感器分频器 */
     uint32_t hall_commutation_delay_time; /**< 换向延时时间(单位: 时钟周期) */
 };
/**
 * @brief 每一个不同平台的 TIM 都有自己独特的配置
 * @note  例如独特的寄存器基地址，独特的时钟分频，独特的计数模式等
 * @note  完整定义在平台实现文件 (如 hal_tim.c) 中，头文件仅暴露不透明指针
 */
struct hal_platform_unique_cfg;
typedef struct hal_platform_unique_cfg hal_platform_unique_config;
                                        /*重定义的tim结构体*/
/*==========================================================================================================================================================*/
typedef struct hal_tim_base_cfg         hal_tim_base_config;
typedef struct hal_input_capture_cfg    hal_input_capture_config;
typedef struct hal_output_compare_cfg   hal_output_compare_config;
typedef struct hal_tim_chn_config_t     hal_tim_channel_config;
typedef struct hal_tim_pin_cfg          hal_tim_pin_config;
typedef struct hal_bdtr_cfg             hal_bdtr_config;
typedef struct hal_hall_cfg             hal_hall_config;
/*===========================================================================================================================================================*/

                                                            /*定时器实体与 API*/
/*===========================================================================================================================================================*/
/**
 * @brief TIM 主机配置结构体
 * @note 主机配置结构体，用于简化配置，将定时器基础配置，输入捕获配置，输出比较配置，通道配置合并为一个结构体
*/
typedef struct hal_tim_host_config
{
    hal_tim_base_config         base;
    hal_input_capture_config    input_capture;
    hal_output_compare_config   output_compare;
    hal_tim_channel_config      channel;
    hal_tim_pin_config          pin;
    hal_encoder_config          encoder;
    hal_bdtr_config             bdtr;
    hal_hall_config             hall;
}hal_tim_host_config;

/**
 * @brief TIM 设备结构体
 * @note 聚合具体的通道硬件状态，用于 VFS 与 HAL 层进行高效的对象绑定
*/
typedef struct hal_tim_device
{
    hal_tim_host_config*        host;
    hal_platform_unique_config* unique;
    int (*init_func)(void* tim_handle, void* cfg_ptr,struct hal_tim_device* pdev);
    int (*close_func)(void* tim_handle,struct hal_tim_device* pdev);
} hal_tim_device;

/**
 * @brief 绑定平台唯一配置
 * @note 绑定平台唯一配置，将平台唯一配置绑定到定时器设备上
 * @param pdev 定时器设备指针
 * @param unique_cfg 需要绑定的平台唯一配置指针
 * @return 错误码
*/
int COMPAT_WARN_UNUSED_RESULT hal_tim_device_init(hal_tim_device* pdev, hal_platform_unique_config* unique_cfg,hal_tim_host_config* host);

/**
 * @brief 定时器设备反初始化
 * @note 反初始化定时器设备，包括定时器基础配置，输入捕获配置，输出比较配置，通道配置，平台唯一配置
*/
int COMPAT_WARN_UNUSED_RESULT hal_tim_device_deinit(hal_tim_device* pdev);

/*
 * @brief 定时器打开
 * @note 打开定时器，包括初始化定时器，绑定平台唯一配置，绑定主机配置
 * @param dev 定时器设备指针
 * @return 错误码
*/
int COMPAT_WARN_UNUSED_RESULT hal_tim_open(hal_tim_device* pdev);

/**
 * @brief 定时器关闭
 * @note 关闭定时器，包括定时器基础配置，输入捕获配置，输出比较配置，通道配置，平台唯一配置
 * @param dev 定时器设备指针
 * @return 错误码
*/
int COMPAT_WARN_UNUSED_RESULT hal_tim_close(hal_tim_device* pdev);

/**
 * @brief 定时器PWM更新
 * @note 更新定时器PWM，包括频率，占空比
*/
int COMPAT_WARN_UNUSED_RESULT hal_tim_pwm_update(hal_tim_device* pdev, uint32_t frequency, uint32_t duty);

/**
 * @brief 定时器中断配置
 * @note 配置定时器中断，包括中断配置
*/
int COMPAT_WARN_UNUSED_RESULT hal_tim_interrupt_config(hal_tim_device* pdev, uint32_t interrupt_config);

/**
 * @brief 获取定时器当前计数值
 * @note 直接读取底层硬件计数器(CNT)寄存器的当前值，用于时间戳标记或高精度测量
 * @param value 当前计数值
 * @return int 错误码
 */
int COMPAT_WARN_UNUSED_RESULT hal_tim_get_counter(const hal_tim_device* pdev, uint32_t* value);

/**
 * @brief 获取捕获寄存器值
 * @note 在输入捕获中断发生后，调用此接口拉取特定通道锁存的硬件计数值
 * @param channel 通道编号
 * @param value 捕获寄存器值
 * @return int 错误码
 */
int COMPAT_WARN_UNUSED_RESULT hal_tim_get_capture_value(const hal_tim_device* pdev, uint32_t channel, uint32_t* value);
/**
 * @brief 定时器强制停止
 * @note 强制停止定时器，包括定时器基础配置，输入捕获配置，输出比较配置，通道配置，平台唯一配置
*/
int COMPAT_WARN_UNUSED_RESULT hal_tim_force_stop(hal_tim_device* pdev);

/**
 * @brief 切换定时器为编码器接口模式
 * @note 将指定的两个物理通道配置为正交解码模式，硬件自动根据 A/B 相相位差增减 CNT
 * @param dev 指向 TIM 设备结构体的指针
 * @param encoder_mode 解码模式（0: 仅在IA沿计数, 1: 仅在IB沿计数, 2: 在IA和IB双沿计数-4倍频）
 */
int COMPAT_WARN_UNUSED_RESULT hal_tim_encoder_start(hal_tim_device* pdev, uint32_t encoder_mode);
#ifdef __cplusplus
}
#endif

#endif /* HAL_tim_H */
