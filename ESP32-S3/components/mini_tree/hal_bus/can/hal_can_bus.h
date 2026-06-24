#ifndef HAL_CAN_BUS_H
#define HAL_CAN_BUS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

                                                            /*波特率与帧格式枚举*/
/*===========================================================================================================================================================*/
/* CAN 波特率 */
typedef enum
{
    HAL_CAN_SPEED_125K,     /* 125 kbit/s */
    HAL_CAN_SPEED_250K,     /* 250 kbit/s */
    HAL_CAN_SPEED_500K,     /* 500 kbit/s */
    HAL_CAN_SPEED_1M,       /* 1 Mbit/s */
} hal_can_speed_t;

/* 帧格式 */
typedef enum
{
    HAL_CAN_FRAME_STANDARD,     /* 标准帧, 11-bit ID */
    HAL_CAN_FRAME_EXTENDED,     /* 扩展帧, 29-bit ID */
} hal_can_frame_t;
/*===========================================================================================================================================================*/

                                                            /*消息与配置结构*/
/*===========================================================================================================================================================*/
/* CAN 消息结构 */
struct hal_can_msg
{
    uint32_t        id;             /* ID(标准帧使用低11位) */
    hal_can_frame_t frame_type;     /* 帧格式 */
    uint8_t         dlc;            /* 数据长度, 0-8 */
    uint8_t         data[8];        /* 数据 */
};

/* CAN 控制器配置 */
struct hal_can_config
{
    int             can_id;     /* CAN 控制器编号, 0 = CAN1 */
    hal_can_speed_t speed;      /* 波特率 */
    int             tx_pin;     /* TX 引脚 */
    int             rx_pin;     /* RX 引脚 */
};

/* 硬件过滤器 */
struct hal_can_filter
{
    uint32_t id;        /* 期望 ID */
    uint32_t mask;      /* 掩码(对应位为0表示忽略) */
    int      is_ext;    /* 0 = 标准帧, 1 = 扩展帧 */
};

/* 接收回调 */
typedef void (*hal_can_rx_callback_t)(struct hal_can_bus* can, const struct hal_can_msg* msg, void* user_data);
/*===========================================================================================================================================================*/

                                                            /*总线实体与 API*/
/*===========================================================================================================================================================*/
struct hal_can_bus
{
    int (*init)(struct hal_can_bus* can, const struct hal_can_config* cfg);
    int (*send)(struct hal_can_bus* can, const struct hal_can_msg* msg, uint32_t timeout_ms);
    int (*recv)(struct hal_can_bus* can, struct hal_can_msg* msg, uint32_t timeout_ms);
    int (*set_filter)(struct hal_can_bus* can, const struct hal_can_filter* filter, int count);
    int (*set_rx_callback)(struct hal_can_bus* can, hal_can_rx_callback_t cb, void* user_data);
    int (*deinit)(struct hal_can_bus* can);
    void* _impl;
};

void hal_can_bus_init_struct(struct hal_can_bus* can);
/*===========================================================================================================================================================*/

                                                            /*安全停机*/
/*===========================================================================================================================================================*/
void hal_can_force_stop(void);
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_CAN_BUS_H */
