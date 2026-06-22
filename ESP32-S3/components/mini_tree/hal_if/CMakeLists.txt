# ==============================================================================
# hal_if — 普通外设 HAL (GPIO / UART / ADC / PWM / Flash 等, 非共享总线)
# 总线实例见 hal_inst.
# ESP-IDF 构建: 源文件在父 CMakeLists.txt 的 idf_component_register() 中指定。
# ==============================================================================

add_library(hal_if STATIC
    "src/hal_gpio.c"
    "src/hal_if_dummy.c"
    "src/hal_cpu_amp.c"
)

target_include_directories(hal_if
    PUBLIC
        "include"
)

target_link_libraries(hal_if driver
)
