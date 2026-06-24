# ==============================================================================
# hal_bus — 总线抽象 + 平台实例 (SPI / I2C / I2S / CAN / USB / PCIe)
# ==============================================================================

include("${CMAKE_CURRENT_LIST_DIR}/paths.cmake")

add_library(hal_bus STATIC
    "hal_bus_dummy.c"
    "spi/spi_core.c"
    "spi/spi_controller_esp32.c"
)

target_include_directories(hal_bus PUBLIC ${HAL_BUS_INCLUDE_DIRS})

target_include_directories(hal_bus
    PRIVATE
        "${PROJECT_SOURCE_DIR}/board/include"
        "${CMAKE_BINARY_DIR}/generated/board/mini_tree"
        "${PROJECT_SOURCE_DIR}/core/include"
        "${PROJECT_SOURCE_DIR}/osal/include"
)

target_link_libraries(hal_bus PRIVATE osal core)
