# ==============================================================================
# hal_bus — 总线抽象接口 (SPI / I2C / I2S / CAN / USB / PCIe)
# 纯总线 vtable 与配置, 不依赖 device / VFS.
# ==============================================================================

add_library(hal_bus STATIC
    "src/hal_bus_dummy.c"
)

target_include_directories(hal_bus
    PUBLIC
        "include"
)
