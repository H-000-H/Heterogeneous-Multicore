/* SPDX-License-Identifier: Apache-2.0 */
/*
 * GPIO HAL 层 — 硬件抽象接口,硬件直投层
 * @note 所有接口设计为平台无关，由具体芯片平台(如 STM32, ESP32, CH307)进行底层硬实现。
 * @note 由于GPIO是快速热路径外设所以GPIO的初始化与配置应该尽量在硬件直投层完成
 * @note 文件约定：返回值不允许void，必须使用int，并且错误码必须使用VFS.h中的错误码 
 * @note 返回值不允许void，必须使用int，并且错误码必须使用VFS.h中的错误码 
 * @note 接收的参数必须为指针，并且必须为合法的指针，不能为空指针
 * @note 禁止使用enum,enum的问题dts已经解决没必要在hal层重复定义去映射enum不直观而且麻烦还容易出错
 */
 #ifndef HAL_GPIO_H
 #define HAL_GPIO_H
 
 #include <stdint.h>
 #include <stdbool.h>
 #include "compiler_compat.h"
 #include "VFS.h"
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
                                                             /*GPIO 电平与模式配置*/
 /*===========================================================================================================================================================*/
 #define HAL_GPIO_HIGH_LEVEL 1
 #define HAL_GPIO_LOW_LEVEL  0
 
 #ifndef HAL_GPIO_DEV_POOL_SIZE
 #define HAL_GPIO_DEV_POOL_SIZE 16
 #endif
 
 /**
  * @brief GPIO 配置
  * @note 用于配置GPIO的电气特性
  * @param mode 模式
  * @param pull 上拉/下拉
  * @param speed 速度
  * @param output_type 输出类型
  * @param af 复用功能
  */
 struct hal_gpio_cfg
 {
     uint32_t mode;        /**< 模式 */
     uint32_t pull;        /**< 上拉/下拉 */
     uint32_t speed;       /**< 速度 */
     uint32_t output_type; /**< 输出类型 */
     uint32_t af;          /**< 复用功能 */
 };
 /*===========================================================================================================================================================*/
 
 /**
  * @brief typedef 设备配置
  * @note  用于上层调用，避免重复定义(仅热路径可以使用该类型冷路径必须struct hal_x_cfg)
  */
 typedef struct hal_gpio_cfg hal_gpio_config;
 
 typedef struct
 {
     uintptr_t               port;        /**< 端口基地址 */
     uint16_t                pin;         /**< 引脚编号 */
     uint32_t                clk_rcc_bit; /**< 时钟总线/RCC位 */
     hal_gpio_config         cfg;         /**< 配置 */
     bool                    is_used;     /**< 运行时激活状态 (VFS probe 置 true) */
 } hal_gpio_dev_t;
 /*===========================================================================================================================================================*/
 
                                                             /*fast path (实现在 hal_gpio_*.c, 零分支零查表)*/
 /*===========================================================================================================================================================*/
 /**
  * @brief 快路径: 设置 GPIO 输出电平
  * @param pdev   GPIO 对象指针
  * @param level 目标电平 (1=高, 0=低)
  * @return 成功返回 VFS_OK, pdev 为空返回 VFS_ERR_INVAL
  */
 int hal_gpio_fast_set_level(hal_gpio_dev_t* pdev, int level) COMPAT_WARN_UNUSED_RESULT;
 
 /**
  * @brief 快路径: 读取 GPIO 当前输入/输出引脚的实际电平状态
  * @param pdev       GPIO 对象指针
  * @param level_out 用于回传电平的指针 (1=高, 0=低)
  * @return 成功返回 VFS_OK, pdev 或 level_out 为空返回 VFS_ERR_INVAL
  */
 int hal_gpio_fast_get_level(hal_gpio_dev_t* pdev, int *level_out) COMPAT_WARN_UNUSED_RESULT;
 
 /**
  * @brief 快路径: 翻转 GPIO 输出电平
  * @param pdev GPIO 对象指针
  * @return 成功返回 VFS_OK, pdev 为空返回 VFS_ERR_INVAL
  */
 int hal_gpio_fast_toggle(hal_gpio_dev_t* pdev) COMPAT_WARN_UNUSED_RESULT;
 /*===========================================================================================================================================================*/
 
                                                             /*HAL API (基于对象指针)*/
 /*===========================================================================================================================================================*/
 /**
  * @brief GPIO 初始化
  * @param pdev GPIO 对象指针
  * @return 成功返回 VFS_OK, pdev 或内部配置为空返回 VFS_ERR_INVAL
  */
 int hal_gpio_init(hal_gpio_dev_t* pdev) COMPAT_WARN_UNUSED_RESULT;
 
 /**
  * @brief GPIO 释放
  * @param pdev GPIO 对象指针
  * @return 成功返回 VFS_OK, pdev 为空返回 VFS_ERR_INVAL
  */
 int hal_gpio_deinit(hal_gpio_dev_t* pdev) COMPAT_WARN_UNUSED_RESULT;
 
 /**
  * @brief GPIO 设置模式
  * @param pdev GPIO 对象指针
  * @param mode 模式宏值 (如 LL_GPIO_MODE_OUTPUT)
  * @return 成功返回 VFS_OK
  */
 int hal_gpio_set_mode(hal_gpio_dev_t* pdev, uint32_t mode) COMPAT_WARN_UNUSED_RESULT;
 
 /**
  * @brief GPIO 获取当前模式
  * @param pdev GPIO 对象指针
  * @param mode 用于回传当前模式宏值的指针
  * @return 成功返回 VFS_OK
  */
 int hal_gpio_get_mode(hal_gpio_dev_t* pdev, uint32_t *mode) COMPAT_WARN_UNUSED_RESULT;
 
 /**
  * @brief GPIO 设置上拉/下拉
  * @param pdev GPIO 对象指针
  * @param pull 上拉/下拉宏值 (如 LL_GPIO_PULL_UP)
  * @return 成功返回 VFS_OK
  */
 int hal_gpio_set_pull(hal_gpio_dev_t* pdev, uint32_t pull) COMPAT_WARN_UNUSED_RESULT;
 
 /**
  * @brief GPIO 获取当前上拉/下拉配置
  * @param pdev GPIO 对象指针
  * @param pull 用于回传上拉/下拉宏值的指针
  * @return 成功返回 VFS_OK
  */
 int hal_gpio_get_pull(hal_gpio_dev_t* pdev, uint32_t *pull) COMPAT_WARN_UNUSED_RESULT;
 
 /**
  * @brief GPIO 设置速度
  * @param pdev GPIO 对象指针
  * @param speed 速度宏值 (如 LL_GPIO_SPEED_FREQ_HIGH)
  * @return 成功返回 VFS_OK
  */
 int hal_gpio_set_speed(hal_gpio_dev_t* pdev, uint32_t speed) COMPAT_WARN_UNUSED_RESULT;
 
 /**
  * @brief GPIO 获取当前速度配置
  * @param pdev GPIO 对象指针
  * @param speed 用于回传速度宏值的指针
  * @return 成功返回 VFS_OK
  */
 int hal_gpio_get_speed(hal_gpio_dev_t* pdev, uint32_t *speed) COMPAT_WARN_UNUSED_RESULT;
 
 /**
  * @brief GPIO 设置输出类型
  * @param pdev GPIO 对象指针
  * @param output_type 输出类型宏值 (如 LL_GPIO_OUTPUT_PUSHPULL)
  * @return 成功返回 VFS_OK
  */
 int hal_gpio_set_output_type(hal_gpio_dev_t* pdev, uint32_t output_type) COMPAT_WARN_UNUSED_RESULT;
 
 /**
  * @brief GPIO 获取当前输出类型配置
  * @param pdev GPIO 对象指针
  * @param output_type 用于回传输出类型宏值的指针
  * @return 成功返回 VFS_OK
  */
 int hal_gpio_get_output_type(hal_gpio_dev_t* pdev, uint32_t *output_type) COMPAT_WARN_UNUSED_RESULT;
 
 /**
  * @brief GPIO 设置复用功能寄存器值(AFR)
  * @param pdev GPIO 对象指针
  * @param af 复用功能宏值 (如 LL_GPIO_AF_1)
  * @return 成功返回 VFS_OK
  */
 int hal_gpio_set_af(hal_gpio_dev_t* pdev, uint32_t af) COMPAT_WARN_UNUSED_RESULT;
 
 /**
  * @brief GPIO 获取当前引脚的复用功能寄存器值(AFR)
  * @param pdev GPIO 对象指针
  * @param af 用于回传复用功能宏值的指针
  * @return 成功返回 VFS_OK
  */
 int hal_gpio_get_af(hal_gpio_dev_t* pdev, uint32_t *af) COMPAT_WARN_UNUSED_RESULT;
 
 /**
  * @brief GPIO 设置复用功能并自动将引脚切换为复用模式
  * @param pdev GPIO 对象指针
  * @param af 复用功能宏值
  * @return 成功返回 VFS_OK
  */
 int hal_gpio_set_af_mode(hal_gpio_dev_t* pdev, uint32_t af) COMPAT_WARN_UNUSED_RESULT;
 /*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif
#endif /* HAL_GPIO_H */