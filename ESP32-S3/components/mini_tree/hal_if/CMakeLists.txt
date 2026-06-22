# ==============================================================================
# hal_if — 普通外设 HAL (按功能分子目录; 总线见 hal_bus)
#   gpio/ cpu/ uart/ pwm/ pulse/ analog/ storage/ system/ soc/<chip>/
# ESP-IDF 构建: 源文件在父 CMakeLists.txt 的 idf_component_register() 中指定。
# ==============================================================================

include("${CMAKE_CURRENT_LIST_DIR}/paths.cmake")

add_library(hal_if STATIC
    "gpio/hal_gpio_esp32.c"
    "cpu/hal_cpu_amp.c"
    "hal_if_dummy.c"
)

target_include_directories(hal_if PUBLIC ${HAL_IF_INCLUDE_DIRS})

target_link_libraries(hal_if driver)
