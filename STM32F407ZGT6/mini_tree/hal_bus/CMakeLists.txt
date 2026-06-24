# ==============================================================================
# hal_bus — 总线抽象 (SPI / I2C / I2S / CAN / USB / PCIe)
# 布局对齐 ESP32-S3/components/mini_tree 基准
# ST/CH: SPI 实现由工程 hal/ 提供; 此处仅保留头文件与 dummy
# ==============================================================================

include("${CMAKE_CURRENT_LIST_DIR}/paths.cmake")

add_library(hal_bus STATIC
    "hal_bus_dummy.c"
)

target_include_directories(hal_bus PUBLIC ${HAL_BUS_INCLUDE_DIRS})
