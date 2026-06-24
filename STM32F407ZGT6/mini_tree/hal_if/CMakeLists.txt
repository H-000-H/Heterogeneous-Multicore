# ==============================================================================
# hal_if — 普通外设 HAL (按功能分子目录; 总线见 hal_bus)
# 布局对齐 ESP32-S3/components/mini_tree 基准
# ==============================================================================

include("${CMAKE_CURRENT_LIST_DIR}/paths.cmake")

add_library(hal_if STATIC
    "cpu/hal_cpu_amp.c"
    "soc/stm32f407/hal_stm32f407.c"
    "hal_if_dummy.c"
)

target_include_directories(hal_if PUBLIC ${HAL_IF_INCLUDE_DIRS}
    "${CMAKE_CURRENT_LIST_DIR}/soc/stm32f407"
)

target_link_libraries(hal_if PUBLIC hal_bus)
