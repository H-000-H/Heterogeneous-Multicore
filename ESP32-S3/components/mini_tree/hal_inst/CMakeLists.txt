# ==============================================================================
# hal_inst — 总线实例层 (ctx、DMA 池、平台实现)
# ==============================================================================

add_library(hal_inst STATIC
    "src/hal_spi.c"
)

target_include_directories(hal_inst
    PUBLIC
        "include"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/board/include"
        "${CMAKE_BINARY_DIR}/generated/board/mini_tree"
        "${PROJECT_SOURCE_DIR}/core/include"
        "${PROJECT_SOURCE_DIR}/osal/include"
)

target_link_libraries(hal_inst
    PRIVATE
        osal
        core
)
