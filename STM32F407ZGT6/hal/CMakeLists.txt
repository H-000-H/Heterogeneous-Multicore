# STM32F407 板级 HAL (mini_tree 平台实现)
# SPI 仅主机模式；时钟/GPIO 由 CubeMX 生成代码提供

include("${CMAKE_CURRENT_LIST_DIR}/../mini_tree/hal_if/paths.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../mini_tree/hal_bus/paths.cmake")

set(STM32F407_HAL_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/hal_spi_stm32.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/hal_uart_stm32.c"
)

add_library(stm32f407_hal STATIC ${STM32F407_HAL_SRCS})

target_include_directories(stm32f407_hal
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Core/Inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../mini_tree/board/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../mini_tree/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../mini_tree/osal/include"
        ${HAL_BUS_INCLUDE_DIRS}
        ${HAL_IF_INCLUDE_DIRS}
)

target_compile_definitions(stm32f407_hal PUBLIC STM32F407xx)

target_link_libraries(stm32f407_hal PUBLIC STM32_Drivers)
