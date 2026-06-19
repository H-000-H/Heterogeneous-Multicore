#ifndef DT_CONFIG_GEN_H
#define DT_CONFIG_GEN_H

/* IDE / pre-build fallback for DTC_GEN_* macros.
 * Build overrides via build/generated/board/mini_tree/dt_config_gen.h
 * (CMake puts generated dir ahead of board/include). */

#define DTC_GEN_COUNT_ESP32_SPI                      1
#define DTC_GEN_COUNT_ESP32_SPI_MASTER               1
#define DTC_GEN_COUNT_ESP32_WS2812                   1
#define DTC_GEN_COUNT_ESPRESSIF_ESP32_S3_DEVKITC_1   1
#define DTC_GEN_COUNT_ESPRESSIF_XTENSA_LX7           1
#define DTC_GEN_COUNT_HETEROGENEOUS_FFT_SPI_SLAVE    1
#define DTC_GEN_COUNT_HETEROGENEOUS_W25Q64_MASTER    1
#define DTC_GEN_COUNT_SIMPLE_BUS                     1
#define DTC_GEN_CPU_CLOCK_HZ                         240000000
#define DTC_GEN_TICK_RATE_HZ                         1000
#define DTC_GEN_HEAP_SIZE                            32768

#endif /* DT_CONFIG_GEN_H */
