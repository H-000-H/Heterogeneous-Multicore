#ifndef BOARD_NODES_H
#define BOARD_NODES_H

#include <stdint.h>

/* ===== 设备 ID 枚举 (自动生成) ===== */
typedef enum {
    DEV_ID_ = 0,
    DEV_ID_CPU_0 = 1,
    DEV_ID_SOC = 2,
    DEV_ID_GPIO_0 = 3,
    DEV_ID_GPIOS_PIN_0 = 4,
    DEV_ID_LED_0 = 5,
    DEV_ID_SPI_0 = 6,
    DEV_ID_FFT_0 = 7,
    DEV_ID_SPI_1 = 8,
    DEV_ID_W25Q64_0 = 9,
    DEV_ID_COUNT = 10
} device_id_t;

/* ===== alias 宏 ===== */
#define ALIAS_LED0      DEV_ID_LED_0

#endif /* BOARD_NODES_H */
