# mini_tree component for ESP-IDF (ESP32-S3)
# Lightweight mode: device tree + OSAL + bare metal only.
# Configuration via Kconfig (.config)
#
# NOTE: Use CMAKE_CURRENT_LIST_DIR for paths — ESP-IDF's two-phase
# processing means CMAKE_CURRENT_SOURCE_DIR is NOT the component dir
# during the requirements-gathering phase.

# ═══════════════════════════════════════════════════════════════
# OSAL source selection from .config (kconfiglib format)
# ═══════════════════════════════════════════════════════════════
set(KCONFIG_DOT "${CMAKE_CURRENT_LIST_DIR}/.config")

file(STRINGS "${KCONFIG_DOT}" CONFIG_OSAL_ENTRY
     REGEX "^CONFIG_OSAL_[A-Z_]+=y$")
file(STRINGS "${KCONFIG_DOT}" CONFIG_SYSTEM_ENTRY
     REGEX "^CONFIG_SYSTEM_(C|CPP)=y$")

# Force cmake reconfigure when .config changes
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${KCONFIG_DOT}")

# ── OSAL 后端选择 ──
if(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_FREERTOS=y")
    set(OSAL_SRCS   "osal/src/osal_freertos.c")
    set(OSAL_DEFINE CONFIG_OSAL_FREERTOS)

elseif(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_RTTHREAD=y")
    set(OSAL_SRCS   "osal/src/osal_rtthread.c")
    set(OSAL_DEFINE CONFIG_OSAL_RTTHREAD)

else()
    # Default: NULL (bare metal)
    set(OSAL_SRCS   "osal/src/osal_null.c")
    set(OSAL_DEFINE CONFIG_OSAL_NULL)
endif()

file(STRINGS "${KCONFIG_DOT}" CONFIG_HAL_GPIO_ENTRY
     REGEX "^CONFIG_HAL_GPIO_(STM32|CH32|ESP32)=y$")
if(CONFIG_HAL_GPIO_ENTRY STREQUAL "CONFIG_HAL_GPIO_STM32=y")
    set(HAL_GPIO_SRC "hal_if/gpio/hal_gpio_stm32.c")
elseif(CONFIG_HAL_GPIO_ENTRY STREQUAL "CONFIG_HAL_GPIO_CH32=y")
    set(HAL_GPIO_SRC "hal_if/gpio/hal_gpio_ch32.c")
else()
    set(HAL_GPIO_SRC "hal/gpio/hal_gpio_esp32.c")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/hal/paths.cmake")

set(HAL_SRCS
    ${HAL_GPIO_SRC}
    "hal/pulse/hal_pulse_engine_esp32s3.c"
    "hal/soc/esp32s3/hal_esp32s3.c"
    "hal/cpu/hal_cpu_amp.c"
    "hal/hal_if_dummy.c"
    "hal/spi/hal_spi.c"
    "hal/uart/hal_uart.c"
    "bus/dma/dma_core.c"
    "bus/dma/dma_esp32.c"
)

# ── Board layer (device tree, driver, config store, tasks) ──
set(BOARD_SRCS
    "board/src/board_device.c"
    "board/src/board_dma.c"
    "board/src/dev_lifecycle.c"
    "board/src/board_driver.c"
    "board/src/config_store.c"
    "board/src/task_config.c"
    "board/src/task_utils.c"
)

# ── Generated board files (dtc-lite output) ──
set(BOARD_DTS            "${CMAKE_CURRENT_LIST_DIR}/board/dts/esp32-s3-devkitc-1.dts")
set(BOARD_DTSI_DIR       "${CMAKE_CURRENT_LIST_DIR}/board/dtsi")
set(BOARD_DT_BINDINGS_DIR "${CMAKE_CURRENT_LIST_DIR}/board/dt-bindings")
set(DTC_LITE             "${CMAKE_CURRENT_LIST_DIR}/tools/dtc-lite.py")
set(GENERATED_BOARD_DIR  "${CMAKE_BINARY_DIR}/generated/board/mini_tree")

set(GEN_SRCS
    "${GENERATED_BOARD_DIR}/board_devtable.c"
    "${GENERATED_BOARD_DIR}/board_probe.c"
)

set(GEN_HDRS
    "${GENERATED_BOARD_DIR}/board_nodes.h"
    "${GENERATED_BOARD_DIR}/board_devtable.h"
    "${GENERATED_BOARD_DIR}/board_handles.h"
    "${GENERATED_BOARD_DIR}/dt_config_gen.h"
)

set(SCRUBBER_GEN_DIR "${CMAKE_BINARY_DIR}/generated/scrubber/mini_tree")
set(SCRUBBER_CRC_HDR "${SCRUBBER_GEN_DIR}/system_scrubber_crc_gen.h")

file(MAKE_DIRECTORY "${GENERATED_BOARD_DIR}")
file(MAKE_DIRECTORY "${SCRUBBER_GEN_DIR}")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ── Core layer (event bus, buffer pool, production log) ──
set(CORE_SRCS
    "core/src/event_bus.c"
    "core/src/buffer_pool.c"
    "core/src/production_log.c"
    "core/src/printf_output.c"
)

# ── System layer (C / C++ backend from Kconfig) ──
if(CONFIG_SYSTEM_ENTRY STREQUAL "CONFIG_SYSTEM_CPP=y")
    set(SYSTEM_SRCS
        "system_cpp/src/system_cmd.cpp"
        "system_cpp/src/system_init.cpp"
        "system_cpp/src/system_scrubber.cpp"
        "system_cpp/src/system_wdt.cpp"
        "system_cpp/src/task_manager.cpp"
        "system_cpp/src/safe_state.c"
    )
else()
    set(SYSTEM_SRCS
        "system_c/src/system_init.c"
        "system_c/src/system_scrubber.c"
        "system_c/src/system_wdt.c"
        "system_c/src/task_manager.c"
        "system_cpp/src/safe_state.c"
    )
endif()

# ── VFS 设备驱动 ──
set(DRIVER_SRCS
    "drivers/ws2812/ws2812_drv.c"
    "drivers/flash/w25q64_spi_drv.c"
    "vfs/gpio/vfs-gpio.c"
    "bus/bus.c"
    "bus/spi/spi_bus.c"
    "bus/uart/uart_bus.c"
    "vfs/spi/spi_vfs.c"
    "vfs/uart/uart_vfs.c"
)

# ── Algorithm ──
set(ALGO_SRCS
    "algorithm/buffer/circle_fifo_buffer.c"
)

# ═══════════════════════════════════════════════════════════════
idf_component_register(
    SRCS
        ${OSAL_SRCS}
        ${HAL_SRCS}
        ${BOARD_SRCS}
        ${GEN_SRCS}
        ${CORE_SRCS}
        ${SYSTEM_SRCS}
        ${DRIVER_SRCS}
        ${ALGO_SRCS}
    INCLUDE_DIRS
        "."
        "${GENERATED_BOARD_DIR}"
        "board/dt-bindings/hw"
        "board/include"
        "core/include"
        "bus"
        "bus/dma"
        "hal/spi"
        "hal/uart"
        "bus/spi"
        "bus/uart"
        "vfs/spi"
        "vfs/uart"
        ${HAL_INCLUDE_DIRS}
        "osal/include"
        "system_c/include"
        "system_cpp/include"
        "drivers/ws2812"
        "drivers/flash"
        "vfs/gpio"
        "algorithm/buffer"
    REQUIRES
        esp_driver_rmt
        esp_driver_gpio
        esp_driver_spi
        esp_driver_uart
        etlcpp
)

# ═══════════════════════════════════════════════════════════════
# Generate config.h from Kconfig at build time
# Use CMAKE_BINARY_DIR based location (stable across both phases)
# ═══════════════════════════════════════════════════════════════
set(KCONFIG_GEN_DIR "${CMAKE_BINARY_DIR}/generated/kconfig/mini_tree")
set(KCONFIG_OUT     "${KCONFIG_GEN_DIR}/config.h")

add_custom_command(
    OUTPUT  ${KCONFIG_OUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${KCONFIG_GEN_DIR}
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_LIST_DIR}/tools/genconfig.py"
                "${CMAKE_CURRENT_LIST_DIR}/Kconfig"
                ${KCONFIG_GEN_DIR}
                --config "${KCONFIG_DOT}"
    DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/Kconfig"
        "${KCONFIG_DOT}"
        "${CMAKE_CURRENT_LIST_DIR}/tools/genconfig.py"
    COMMENT "Generating config.h from Kconfig"
)

add_custom_target(kconfig_gen DEPENDS ${KCONFIG_OUT})
add_dependencies(${COMPONENT_LIB} kconfig_gen)

# Make generated config.h available to this component
target_include_directories(${COMPONENT_LIB} PUBLIC ${KCONFIG_GEN_DIR})

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ${OSAL_DEFINE}
)
target_linker_script(${COMPONENT_LIB} INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/error_symbols.ld"
)
# ═══════════════════════════════════════════════════════════════
# Generate board DTS tables via dtc-lite (see board/CMakeLists.txt for layout)
# ═══════════════════════════════════════════════════════════════
add_custom_command(
    OUTPUT  ${GEN_SRCS} ${GEN_HDRS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GENERATED_BOARD_DIR}"
    COMMAND ${Python3_EXECUTABLE}
            "${DTC_LITE}" "${BOARD_DTS}" "${GENERATED_BOARD_DIR}"
            "${CMAKE_CURRENT_LIST_DIR}/drivers/ws2812"
            "${CMAKE_CURRENT_LIST_DIR}/drivers/flash"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/spi"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/gpio"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/uart"
            "${CMAKE_CURRENT_LIST_DIR}/bus/spi"
            "${CMAKE_CURRENT_LIST_DIR}/bus/uart"
    DEPENDS
        "${BOARD_DTS}"
        "${BOARD_DTSI_DIR}/esp32s3.dtsi"
        "${BOARD_DTSI_DIR}/esp32s3-gpio.dtsi"
        "${BOARD_DTSI_DIR}/esp32s3-ws2812.dtsi"
        "${BOARD_DTSI_DIR}/esp32s3-spi.dtsi"
        "${BOARD_DTSI_DIR}/esp32s3-uart.dtsi"
        "${BOARD_DT_BINDINGS_DIR}/led/ws2812-timing.h"
        "${BOARD_DT_BINDINGS_DIR}/spi/spi-parameter.h"
        "${CMAKE_CURRENT_LIST_DIR}/drivers/flash/w25q64_spi_drv.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/spi/spi_vfs.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/uart/uart_vfs.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/gpio/vfs-gpio.c"
        "${CMAKE_CURRENT_LIST_DIR}/bus/bus.c"
        "${CMAKE_CURRENT_LIST_DIR}/bus/spi/spi_bus.c"
        "${CMAKE_CURRENT_LIST_DIR}/bus/uart/uart_bus.c"
        "${DTC_LITE}"
    COMMENT "Running dtc-lite on esp32-s3-devkitc-1.dts"
    VERBATIM
)

add_custom_target(board_dts_gen DEPENDS ${GEN_SRCS} ${GEN_HDRS})
add_dependencies(${COMPONENT_LIB} board_dts_gen)
set_source_files_properties(${GEN_SRCS} PROPERTIES GENERATED TRUE)

# ═══════════════════════════════════════════════════════════════
# Scrubber CRC baseline stub (post_build_crc.py overwrites after link)
# ═══════════════════════════════════════════════════════════════
add_custom_command(
    OUTPUT  ${SCRUBBER_CRC_HDR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_LIST_DIR}/tools/system_scrubber_crc_stub.h"
            "${SCRUBBER_CRC_HDR}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/tools/system_scrubber_crc_stub.h"
    COMMENT "Generating system_scrubber_crc_gen.h stub"
    VERBATIM
)

add_custom_target(scrubber_crc_gen DEPENDS ${SCRUBBER_CRC_HDR})
add_dependencies(${COMPONENT_LIB} scrubber_crc_gen)

target_include_directories(${COMPONENT_LIB} PUBLIC "${SCRUBBER_GEN_DIR}")
