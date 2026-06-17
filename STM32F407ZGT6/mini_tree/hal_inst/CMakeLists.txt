# ==============================================================================
# hal_inst — 总线实例层 (ctx、DMA 池、平台绑定)
# ==============================================================================

add_library(hal_inst STATIC
    "src/hal_inst_dummy.c"
)

target_include_directories(hal_inst
    PUBLIC
        "include"
)

target_link_libraries(hal_inst
    PUBLIC
        hal_bus
)
