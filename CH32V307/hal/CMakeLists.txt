# CH32V307 WCH 外设库 (平台 HAL 实现已迁入 mini_tree/hal_if 与 mini_tree/hal_bus)

set(CH32_PERIPH_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/../SRC/Peripheral/src/ch32v30x_gpio.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../SRC/Peripheral/src/ch32v30x_spi.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../SRC/Peripheral/src/ch32v30x_usart.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../SRC/Peripheral/src/ch32v30x_rcc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../SRC/Peripheral/src/ch32v30x_misc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../SRC/Peripheral/src/ch32v30x_dma.c"
)

add_library(ch32_periph STATIC ${CH32_PERIPH_SRCS})

target_include_directories(ch32_periph
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../SRC/Peripheral/inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/../SRC/Core"
        "${CMAKE_CURRENT_SOURCE_DIR}/../SRC/Debug"
        "${CMAKE_CURRENT_SOURCE_DIR}/../SRC/User"
)
