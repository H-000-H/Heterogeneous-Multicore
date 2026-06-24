#ifndef HAL_USB_BUS_H
#define HAL_USB_BUS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

                                                            /*模式速率端点枚举*/
/*===========================================================================================================================================================*/
/* USB 工作模式 */
typedef enum
{
    HAL_USB_MODE_DEVICE,    /* 设备模式 */
    HAL_USB_MODE_HOST,      /* 主机模式 */
    HAL_USB_MODE_OTG,       /* OTG 模式 */
} hal_usb_mode_t;

/* USB 速率 */
typedef enum
{
    HAL_USB_SPEED_LOW,      /* 低速 1.5 Mbps */
    HAL_USB_SPEED_FULL,     /* 全速 12 Mbps */
    HAL_USB_SPEED_HIGH,     /* 高速 480 Mbps */
} hal_usb_speed_t;

/* 端点类型 */
typedef enum
{
    HAL_USB_EP_CTRL,        /* 控制端点 */
    HAL_USB_EP_BULK,        /* 批量端点 */
    HAL_USB_EP_INT,         /* 中断端点 */
    HAL_USB_EP_ISO,         /* 同步端点 */
} hal_usb_ep_type_t;
/*===========================================================================================================================================================*/

                                                            /*控制器与端点配置*/
/*===========================================================================================================================================================*/
/* USB 控制器配置 */
struct hal_usb_config
{
    int                 usb_id;     /* USB 控制器编号 */
    hal_usb_mode_t      mode;       /* 工作模式 */
    hal_usb_speed_t     speed;      /* 目标速率 */
    int                 dp_pin;     /* DP 引脚 */
    int                 dm_pin;     /* DM 引脚 */
    int                 vbus_pin;   /* VBUS 检测引脚, -1 = 未用 */
};

/* 端点配置 */
struct hal_usb_ep_config
{
    int              ep_addr;        /* 端点地址, bit7 = 方向(0=OUT, 1=IN) */
    hal_usb_ep_type_t type;          /* 端点类型 */
    int              max_pkt;        /* 最大包长 */
    uint32_t         fifo_size;      /* FIFO 分配大小(字节), IN 端点有效 */
};

/* USB 事件回调, event 取值由实现定义 */
typedef void (*hal_usb_callback_t)(struct hal_usb_bus* usb, int event, void* user_data);
/*===========================================================================================================================================================*/

                                                            /*总线实体*/
/*===========================================================================================================================================================*/
struct hal_usb_bus
{
    int (*init)(struct hal_usb_bus* usb, const struct hal_usb_config* cfg);
    int (*deinit)(struct hal_usb_bus* usb);

    /* 通用控制 */
    int (*connect)(struct hal_usb_bus* usb);
    int (*disconnect)(struct hal_usb_bus* usb);
    int (*set_callback)(struct hal_usb_bus* usb, hal_usb_callback_t cb, void* user_data);

    /* 配置端点 */
    int (*ep_config)(struct hal_usb_bus* usb, const struct hal_usb_ep_config* ep_cfg);
    int (*ep_enable)(struct hal_usb_bus* usb, int ep_addr);
    int (*ep_disable)(struct hal_usb_bus* usb, int ep_addr);

    /* 数据传输 */
    int (*ep_write)(struct hal_usb_bus* usb, int ep_addr, const uint8_t* data, size_t len);
    int (*ep_read)(struct hal_usb_bus* usb, int ep_addr, uint8_t* data, size_t len);

    /* 复位与电源管理 */
    int (*reset)(struct hal_usb_bus* usb);
    int (*suspend)(struct hal_usb_bus* usb);     /* 进入挂起 */
    int (*resume)(struct hal_usb_bus* usb);      /* 退出挂起/远程唤醒 */
    void* _impl;
};
/*===========================================================================================================================================================*/

                                                            /*生命周期与控制 API*/
/*===========================================================================================================================================================*/
void hal_usb_bus_init_struct(struct hal_usb_bus* usb);
/*===========================================================================================================================================================*/

                                                            /*安全停机*/
/*===========================================================================================================================================================*/
void hal_usb_force_stop(void);
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_USB_BUS_H */
