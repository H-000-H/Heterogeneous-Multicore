# ==============================================================================
# board — 板级支持 (设备树、驱动注册、配置存储、VFS)
# ==============================================================================
# 设备树布局 (Linux 写法; dts / dtsi 分目录):
#   board/dts/esp32-s3-devkitc-1.dts  — 板级入口 (#include 头 + / { } + &label)
#   board/dtsi/esp32s3.dtsi           — SoC 根 / { compatible, soc: soc { simple-bus } }
#   board/dtsi/esp32s3-gpio.dtsi     — &soc { gpio@0 / gpio-pin@0 }
#   board/dtsi/esp32s3-ws2812.dtsi    — &soc { led@0 { ... } }
#   board/dtsi/esp32s3-spi.dtsi      — &soc { spi@0 { ... } }
#   board/dt-bindings/                — #include <dt-bindings/...>
#   board/docs/devicetree.md          — 布局、无序全解耦版说明、compatible 属性契约
# ==============================================================================

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(BOARD_DTS             "${CMAKE_CURRENT_SOURCE_DIR}/dts/esp32-s3-devkitc-1.dts")
set(BOARD_DTSI_DIR        "${CMAKE_CURRENT_SOURCE_DIR}/dtsi")
set(BOARD_DT_BINDINGS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dt-bindings")
set(DTC_LITE              "${PROJECT_SOURCE_DIR}/tools/dtc-lite.py")
set(GENERATED_BOARD_DIR   "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(MAKE_DIRECTORY "${GENERATED_BOARD_DIR}")

set(GENERATED_BOARD_SRCS
    "${GENERATED_BOARD_DIR}/board_devtable.c"
    "${GENERATED_BOARD_DIR}/board_probe.c"
)

set(GENERATED_BOARD_HDRS
    "${GENERATED_BOARD_DIR}/board_nodes.h"
    "${GENERATED_BOARD_DIR}/board_devtable.h"
    "${GENERATED_BOARD_DIR}/board_handles.h"
)

add_custom_command(
    OUTPUT  ${GENERATED_BOARD_SRCS} ${GENERATED_BOARD_HDRS}
    COMMAND ${Python3_EXECUTABLE}
            "${DTC_LITE}" "${BOARD_DTS}" "${GENERATED_BOARD_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../drivers/ws2812"
            "${CMAKE_CURRENT_SOURCE_DIR}/../drivers/flash"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/spi"
            "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/gpio"
    DEPENDS
        "${BOARD_DTS}"
        "${BOARD_DTSI_DIR}/esp32s3.dtsi"
        "${BOARD_DTSI_DIR}/esp32s3-gpio.dtsi"
        "${BOARD_DTSI_DIR}/esp32s3-ws2812.dtsi"
        "${BOARD_DTSI_DIR}/esp32s3-spi.dtsi"
        "${BOARD_DTSI_DIR}/esp32s3-uart.dtsi"
        "${BOARD_DT_BINDINGS_DIR}/led/ws2812-timing.h"
        "${BOARD_DT_BINDINGS_DIR}/spi/spi-parameter.h"
        "${BOARD_DT_BINDINGS_DIR}/uart/uart-parameter.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/../drivers/flash/w25q64_spi_drv.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/spi/bus/spi_bus.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/spi/slave/spi_slave_client.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/spi/master/spi_master_client.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../vfs/gpio/vfs-gpio.c"
        "${DTC_LITE}"
    VERBATIM
)

add_custom_target(board_dts_generate
    DEPENDS ${GENERATED_BOARD_SRCS}
)

# ---- 库定义 ----
add_library(board STATIC
    "src/task_config.c"
    "src/task_utils.c"
    "src/board_device.c"
    "src/bus.c"
    "src/board_driver.c"
    "src/config_store.c"
    ${GENERATED_BOARD_SRCS}
)

target_include_directories(board
    PUBLIC
        "include"
        "${GENERATED_BOARD_DIR}"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/core/include"
)

target_link_libraries(board
    PUBLIC
        osal
        core
        hal_bus
        hal_if
)

add_dependencies(board board_dts_generate)
add_dependencies(board kconfig_generate)
set_source_files_properties(${GENERATED_BOARD_SRCS} PROPERTIES GENERATED TRUE)
