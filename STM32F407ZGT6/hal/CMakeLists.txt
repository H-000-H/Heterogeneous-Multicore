# STM32F407 板级 HAL (mini_tree hal_if 实现)
set(STM32F407_HAL_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/hal_stm32f407.c"
)

add_library(stm32f407_hal STATIC ${STM32F407_HAL_SRCS})

target_include_directories(stm32f407_hal
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../mini_tree/hal_bus/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../mini_tree/hal_inst/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../mini_tree/hal_if/include"
)

target_compile_definitions(stm32f407_hal PUBLIC STM32F407xx)

target_link_libraries(stm32f407_hal PUBLIC STM32_Drivers)
