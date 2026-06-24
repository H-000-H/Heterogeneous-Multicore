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
    DEV_ID_DMA_SPI1_RX = 5,
    DEV_ID_DMA_SPI1_TX = 6,
    DEV_ID_DMA_UART4_TX = 7,
    DEV_ID_SPI_0 = 8,
    DEV_ID_W25Q64_0 = 9,
    DEV_ID_UART_0 = 10,
    DEV_ID_COUNT = 11
} device_id_t;

#endif /* BOARD_NODES_H */
