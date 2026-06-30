#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* ── 板级配置聚合层 ──
 *
 * 配置真相源:
 *   1. board/dts/, board/dtsi/, board/dt-bindings/ — 硬件实例与属性
 *   2. dt_config_gen.h      — DTC 编译期聚合 (DTC_GEN_COUNT_*); 构建产物优先于 board/include 内 IDE stub
 *   3. system_scrubber_crc_gen.h — 构建后 CRC 基线
 *   4. config.h (Kconfig)   — 运行时容量 (OSAL 池、栈监控等)
 *
 * 硬件实例与属性请写入 board/dts、board/dtsi、board/dt-bindings.
 */

#include "dt_config_gen.h"

#if defined(__has_include)
#if __has_include("config.h")
#include "config.h"
#endif
#endif

                                                            /*板级默认常量*/
/*===========================================================================================================================================================*/
#define BOARD_MAX_SAFETY_PINS      8
#define BOARD_SAFETY_MAX_CALLBACKS 4

#define BOARD_STACK_ALARM_RATIO_DEFAULT   15  /* 剩余栈 < 15% 时告警 */

#ifndef BOARD_SAFE_STATE_BUZZER_PIN
#define BOARD_SAFE_STATE_BUZZER_PIN    0
#endif
#ifndef BOARD_SAFE_STATE_FAULT_LED_PIN
#define BOARD_SAFE_STATE_FAULT_LED_PIN 0
#endif
/*===========================================================================================================================================================*/

                                                            /*栈监控容量*/
/*===========================================================================================================================================================*/
#ifndef BOARD_STACK_MONITOR_MAX_TASKS
#ifdef CONFIG_BOARD_STACK_MONITOR_MAX_TASKS
#define BOARD_STACK_MONITOR_MAX_TASKS CONFIG_BOARD_STACK_MONITOR_MAX_TASKS
#else
#define BOARD_STACK_MONITOR_MAX_TASKS 8
#endif
#endif
/*===========================================================================================================================================================*/

                                                            /*OSAL 运行时池*/
/*===========================================================================================================================================================*/
#ifndef OSAL_MUTEX_POOL_SIZE
#ifdef CONFIG_OSAL_MUTEX_POOL_SIZE
#define OSAL_MUTEX_POOL_SIZE CONFIG_OSAL_MUTEX_POOL_SIZE
#else
#define OSAL_MUTEX_POOL_SIZE 24
#endif
#endif

#ifndef OSAL_MUTEX_STORAGE_SIZE
#define OSAL_MUTEX_STORAGE_SIZE   128
#endif

#ifndef OSAL_SEM_POOL_SIZE
#define OSAL_SEM_POOL_SIZE        8
#endif
/*===========================================================================================================================================================*/

#endif /* BOARD_CONFIG_H */
