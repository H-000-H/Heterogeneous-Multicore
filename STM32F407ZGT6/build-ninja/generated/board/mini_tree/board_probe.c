#include "board_nodes.h"
#include "board_devtable.h"
#include "device.h"

/* ===== probe 函数声明 ===== */
extern int __attribute__((weak)) board_driver_probe_gpios(struct device* dev);
extern int __attribute__((weak)) board_driver_probe_uart_host_vfs(struct device* dev);

/* ===== remove 函数声明 ===== */
extern int __attribute__((weak)) board_driver_remove_gpios(struct device* dev);
extern int __attribute__((weak)) board_driver_remove_uart_host_vfs(struct device* dev);

static int board_platform_probe(struct device* dev) {
    (void)dev;
    return 0;
}

static const probe_fn_t s_probe_fns[DEV_ID_COUNT] = {
    [DEV_ID_] = board_platform_probe,
    [DEV_ID_CPU_0] = board_platform_probe,
    [DEV_ID_SOC] = board_platform_probe,
    [DEV_ID_GPIO_0] = board_platform_probe,
    [DEV_ID_GPIOS_PIN_0] = board_driver_probe_gpios,
    [DEV_ID_DMA_SPI1_RX] = board_platform_probe,
    [DEV_ID_DMA_SPI1_TX] = board_platform_probe,
    [DEV_ID_DMA_UART4_TX] = board_platform_probe,
    [DEV_ID_SPI_0] = board_platform_probe,
    [DEV_ID_W25Q64_0] = NULL,
    [DEV_ID_UART_0] = board_driver_probe_uart_host_vfs,
};

static const remove_fn_t s_remove_fns[DEV_ID_COUNT] = {
    [DEV_ID_] = NULL,
    [DEV_ID_CPU_0] = NULL,
    [DEV_ID_SOC] = NULL,
    [DEV_ID_GPIO_0] = NULL,
    [DEV_ID_GPIOS_PIN_0] = board_driver_remove_gpios,
    [DEV_ID_DMA_SPI1_RX] = NULL,
    [DEV_ID_DMA_SPI1_TX] = NULL,
    [DEV_ID_DMA_UART4_TX] = NULL,
    [DEV_ID_SPI_0] = NULL,
    [DEV_ID_W25Q64_0] = NULL,
    [DEV_ID_UART_0] = board_driver_remove_uart_host_vfs,
};

static const device_id_t s_probe_order[DEV_ID_COUNT] = {
    DEV_ID_,
    DEV_ID_CPU_0,
    DEV_ID_SOC,
    DEV_ID_GPIO_0,
    DEV_ID_GPIOS_PIN_0,
    DEV_ID_DMA_SPI1_RX,
    DEV_ID_DMA_SPI1_TX,
    DEV_ID_DMA_UART4_TX,
    DEV_ID_SPI_0,
    DEV_ID_UART_0,
    DEV_ID_W25Q64_0,
};

probe_fn_t board_probe_get_fn(device_id_t id) {
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) return NULL;
    return s_probe_fns[id];
}

remove_fn_t board_remove_get_fn(device_id_t id) {
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) return NULL;
    return s_remove_fns[id];
}

const device_id_t* board_probe_order(void) {
    return s_probe_order;
}

int board_probe_order_count(void) {
    return DEV_ID_COUNT;
}

static const device_id_t s_cascade_data[] = {
    DEV_ID_W25Q64_0,
};

static const uint8_t s_cascade_counts[DEV_ID_COUNT] = {
    [DEV_ID_] = 0,
    [DEV_ID_CPU_0] = 0,
    [DEV_ID_SOC] = 0,
    [DEV_ID_GPIO_0] = 0,
    [DEV_ID_GPIOS_PIN_0] = 0,
    [DEV_ID_DMA_SPI1_RX] = 0,
    [DEV_ID_DMA_SPI1_TX] = 0,
    [DEV_ID_DMA_UART4_TX] = 0,
    [DEV_ID_SPI_0] = 1,
    [DEV_ID_W25Q64_0] = 0,
    [DEV_ID_UART_0] = 0,
};

static const uint16_t s_cascade_offset[DEV_ID_COUNT] = {
    [DEV_ID_] = 0,
    [DEV_ID_CPU_0] = 0,
    [DEV_ID_SOC] = 0,
    [DEV_ID_GPIO_0] = 0,
    [DEV_ID_GPIOS_PIN_0] = 0,
    [DEV_ID_DMA_SPI1_RX] = 0,
    [DEV_ID_DMA_SPI1_TX] = 0,
    [DEV_ID_DMA_UART4_TX] = 0,
    [DEV_ID_SPI_0] = 0,
    [DEV_ID_W25Q64_0] = 1,
    [DEV_ID_UART_0] = 1,
};

const device_id_t* board_cascade_get(device_id_t id, int* count) {
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) { *count = 0; return NULL; }
    *count = s_cascade_counts[id];
    return *count ? &s_cascade_data[s_cascade_offset[id]] : NULL;
}

static const device_id_t s_children_data[] = {
    DEV_ID_W25Q64_0,
};

static const uint8_t s_children_counts[DEV_ID_COUNT] = {
    [DEV_ID_] = 0,
    [DEV_ID_CPU_0] = 0,
    [DEV_ID_SOC] = 0,
    [DEV_ID_GPIO_0] = 0,
    [DEV_ID_GPIOS_PIN_0] = 0,
    [DEV_ID_DMA_SPI1_RX] = 0,
    [DEV_ID_DMA_SPI1_TX] = 0,
    [DEV_ID_DMA_UART4_TX] = 0,
    [DEV_ID_SPI_0] = 1,
    [DEV_ID_W25Q64_0] = 0,
    [DEV_ID_UART_0] = 0,
};

static const uint16_t s_children_offset[DEV_ID_COUNT] = {
    [DEV_ID_] = 0,
    [DEV_ID_CPU_0] = 0,
    [DEV_ID_SOC] = 0,
    [DEV_ID_GPIO_0] = 0,
    [DEV_ID_GPIOS_PIN_0] = 0,
    [DEV_ID_DMA_SPI1_RX] = 0,
    [DEV_ID_DMA_SPI1_TX] = 0,
    [DEV_ID_DMA_UART4_TX] = 0,
    [DEV_ID_SPI_0] = 0,
    [DEV_ID_W25Q64_0] = 1,
    [DEV_ID_UART_0] = 1,
};

const device_id_t* board_children_get(device_id_t id, int* count) {
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) { *count = 0; return NULL; }
    *count = s_children_counts[id];
    return *count ? &s_children_data[s_children_offset[id]] : NULL;
}
