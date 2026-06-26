# mini_tree (generic CMake build for non-ESP-IDF projects)
# 目录/源文件布局对齐 ESP32-S3/components/mini_tree 基准

find_package(Python3 REQUIRED COMPONENTS Interpreter)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/etl.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/hal/paths.cmake")

set(KCONFIG_DOT "${CMAKE_CURRENT_LIST_DIR}/.config")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${KCONFIG_DOT}")

file(STRINGS "${KCONFIG_DOT}" CONFIG_OSAL_ENTRY   REGEX "^CONFIG_OSAL_[A-Z_]+=y$")
file(STRINGS "${KCONFIG_DOT}" CONFIG_SYSTEM_ENTRY REGEX "^CONFIG_SYSTEM_(C|CPP)=y$")
file(STRINGS "${KCONFIG_DOT}" CONFIG_HAL_GPIO_ENTRY
     REGEX "^CONFIG_HAL_GPIO_(STM32|CH32|ESP32)=y$")

if(CONFIG_HAL_GPIO_ENTRY STREQUAL "CONFIG_HAL_GPIO_STM32=y")
    set(HAL_GPIO_SRC "hal_if/gpio/hal_gpio_stm32.c")
    set(HAL_CPU_DELAY_SRC "hal_if/cpu/hal_cpu_delay_stm32.c")
    set(HAL_PLATFORM_SRCS
        "hal_if/system/hal_dma_stm32.c"
        "hal_if/uart/hal_uart_stm32.c"
    )
    set(HAL_BUS_SRCS
        "hal_bus/spi/spi_core.c"
        "hal_bus/spi/spi_controller_stm32.c"
    )
elseif(CONFIG_HAL_GPIO_ENTRY STREQUAL "CONFIG_HAL_GPIO_CH32=y")
    set(HAL_GPIO_SRC "hal/gpio/hal_gpio_ch32.c")
    set(HAL_CPU_DELAY_SRC "hal/cpu/hal_cpu_delay_ch32.c")
    set(HAL_PLATFORM_SRCS "")
    set(NEW_ARCH_SRCS
        "hal/spi/hal_spi_ch32.c"
        "hal/uart/hal_uart_ch32.c"
        "hal/system/hal_dma_ch32.c"
        "bus/dma/dma_core.c"
        "bus/dma/dma_ch32.c"
    )
else()
    set(HAL_GPIO_SRC "hal_if/gpio/hal_gpio_esp32.c")
    set(HAL_CPU_DELAY_SRC "")
    set(HAL_PLATFORM_SRCS "")
    set(HAL_BUS_SRCS
        "hal_bus/hal_bus_dummy.c"
    )
endif()

if(NOT DEFINED NEW_ARCH_SRCS)
    set(NEW_ARCH_SRCS "")
endif()

if(NOT DEFINED HAL_PLATFORM_SRCS)
    set(HAL_PLATFORM_SRCS "")
endif()
if(NOT DEFINED HAL_BUS_SRCS)
    set(HAL_BUS_SRCS "")
endif()

set(FREERTOS_SRCS "")

if(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_FREERTOS=y")
    set(OSAL_SRCS "osal/src/osal_freertos.c")
    set(OSAL_DEFINE CONFIG_OSAL_FREERTOS)
    message(STATUS "mini_tree OSAL backend: FreeRTOS")
    set(FREERTOS_ROOT "${CMAKE_CURRENT_LIST_DIR}/lib/freeRTOS")
    if(NOT EXISTS "${FREERTOS_ROOT}/include/FreeRTOS.h")
        message(FATAL_ERROR "FreeRTOS kernel not found: ${FREERTOS_ROOT}")
    endif()
    set(FREERTOS_PORT_DIR "${FREERTOS_ROOT}/portable/GCC/RISC-V")
    set(FREERTOS_SRCS
        "${FREERTOS_ROOT}/tasks.c"
        "${FREERTOS_ROOT}/queue.c"
        "${FREERTOS_ROOT}/list.c"
        "${FREERTOS_ROOT}/timers.c"
        "${FREERTOS_ROOT}/event_groups.c"
        "${FREERTOS_ROOT}/stream_buffer.c"
        "${FREERTOS_ROOT}/portable/Common/mpu_wrappers_v2.c"
        "${FREERTOS_ROOT}/portable/MemMang/heap_4.c"
        "${FREERTOS_PORT_DIR}/port.c"
        "${FREERTOS_PORT_DIR}/portASM.S"
    )
elseif(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_RTTHREAD=y")
    set(OSAL_SRCS "osal/src/osal_rtthread.c")
    set(OSAL_DEFINE CONFIG_OSAL_RTTHREAD)
    message(STATUS "mini_tree OSAL backend: RT-Thread")
else()
    set(OSAL_SRCS "osal/src/osal_null.c")
    set(OSAL_DEFINE CONFIG_OSAL_NULL)
    message(STATUS "mini_tree OSAL backend: bare-metal (null)")
endif()

if(CONFIG_SYSTEM_ENTRY STREQUAL "CONFIG_SYSTEM_CPP=y")
    message(STATUS "mini_tree system backend: C++")
    set(SYSTEM_SRCS
        "system_cpp/src/lifecycle.cpp"
        "system_cpp/src/system_cmd.cpp"
        "system_cpp/src/system_init.cpp"
        "system_cpp/src/system_runtime.cpp"
        "system_cpp/src/system_scrubber.cpp"
        "system_cpp/src/system_wdt.cpp"
        "system_cpp/src/task_manager.cpp"
        "system_cpp/src/safe_state.c"
    )
else()
    message(STATUS "mini_tree system backend: C")
    set(SYSTEM_SRCS
        "system_c/src/system_init.c"
        "system_c/src/system_scrubber.c"
        "system_c/src/system_wdt.c"
        "system_c/src/task_manager.c"
        "system_cpp/src/safe_state.c"
    )
endif()

set(HAL_SRCS
    ${HAL_GPIO_SRC}
    ${HAL_CPU_DELAY_SRC}
    ${HAL_PLATFORM_SRCS}
    ${NEW_ARCH_SRCS}
    "hal/cpu/hal_cpu_amp.c"
    "hal/soc/ch32v307/hal_ch32v307.c"
    "hal/hal_if_dummy.c"
)

if(CONFIG_HAL_GPIO_ENTRY STREQUAL "CONFIG_HAL_GPIO_CH32=y")
    list(APPEND HAL_SRCS "${CMAKE_CURRENT_LIST_DIR}/../SRC/Debug/debug.c")
endif()

set(DRIVER_SRCS
    "drivers/flash/w25q64_spi_drv.c"
    "vfs/gpio/vfs-gpio.c"
    "bus/spi/spi_bus.c"
    "bus/uart/uart_bus.c"
    "vfs/spi/spi_vfs.c"
    "vfs/uart/uart_vfs.c"
)

set(KCONFIG_GEN_DIR "${CMAKE_BINARY_DIR}/generated/kconfig/mini_tree")
set(KCONFIG_OUT     "${KCONFIG_GEN_DIR}/config.h")

set(GENERATED_BOARD_DIR "${CMAKE_BINARY_DIR}/generated/board/mini_tree")
set(SCRUBBER_GEN_DIR    "${CMAKE_BINARY_DIR}/generated/scrubber/mini_tree")
set(SCRUBBER_CRC_HDR    "${SCRUBBER_GEN_DIR}/system_scrubber_crc_gen.h")
set(BOARD_DTS           "${CMAKE_CURRENT_LIST_DIR}/board/dts/ch32v307.dts")
set(BOARD_DTSI_DIR      "${CMAKE_CURRENT_LIST_DIR}/board/dtsi")
set(BOARD_DT_BINDINGS_DIR "${CMAKE_CURRENT_LIST_DIR}/board/dt-bindings")
set(DTC_LITE            "${CMAKE_CURRENT_LIST_DIR}/tools/dtc-lite.py")

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

file(MAKE_DIRECTORY "${GENERATED_BOARD_DIR}")
file(MAKE_DIRECTORY "${SCRUBBER_GEN_DIR}")

add_custom_command(
    OUTPUT  "${KCONFIG_OUT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${KCONFIG_GEN_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tools/genconfig.py"
            "${CMAKE_CURRENT_LIST_DIR}/Kconfig"
            "${KCONFIG_GEN_DIR}"
            --config "${KCONFIG_DOT}"
    DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/Kconfig"
        "${CMAKE_CURRENT_LIST_DIR}/tools/genconfig.py"
        "${KCONFIG_DOT}"
    COMMENT "Generating mini_tree config.h"
    VERBATIM
)

add_custom_target(kconfig_generate DEPENDS "${KCONFIG_OUT}")

if(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_RTTHREAD=y")
    set(MINI_TREE_ROOT "${CMAKE_CURRENT_LIST_DIR}")
    set(MINI_TREE_KCONFIG_GEN_DIR "${KCONFIG_GEN_DIR}")
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/lib/rtthread" mini_tree_rtthread)
endif()

add_custom_command(
    OUTPUT  ${GEN_SRCS} ${GEN_HDRS}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_BOARD_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${DTC_LITE}" "${BOARD_DTS}" "${GENERATED_BOARD_DIR}"
            "${CMAKE_CURRENT_LIST_DIR}/drivers/flash"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/spi"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/gpio"
            "${CMAKE_CURRENT_LIST_DIR}/vfs/uart"
            "${CMAKE_CURRENT_LIST_DIR}/bus/spi"
            "${CMAKE_CURRENT_LIST_DIR}/bus/uart"
    DEPENDS
        "${DTC_LITE}"
        "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/ast.py"
        "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/lexer.py"
        "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/parser.py"
        "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/compiler.py"
        "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/generator.py"
        "${CMAKE_CURRENT_LIST_DIR}/tools/dtc_lite/main.py"
        "${BOARD_DTS}"
        "${BOARD_DTSI_DIR}/ch32v307.dtsi"
        "${BOARD_DTSI_DIR}/ch32v307-gpio.dtsi"
        "${BOARD_DTSI_DIR}/ch32v307-spi.dtsi"
        "${BOARD_DTSI_DIR}/ch32v307-uart.dtsi"
        "${BOARD_DT_BINDINGS_DIR}/gpio/gpio-ctl.h"
        "${BOARD_DT_BINDINGS_DIR}/spi/spi-parameter.h"
        "${BOARD_DT_BINDINGS_DIR}/uart/uart-parameter.h"
        "${CMAKE_CURRENT_LIST_DIR}/drivers/flash/w25q64_spi_drv.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/spi/spi_vfs.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/uart/uart_vfs.c"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/gpio/vfs-gpio.c"
        "${CMAKE_CURRENT_LIST_DIR}/bus/spi/spi_bus.c"
        "${CMAKE_CURRENT_LIST_DIR}/bus/uart/uart_bus.c"
    COMMENT "Running dtc-lite on ch32v307.dts"
    VERBATIM
)

add_custom_command(
    OUTPUT  "${SCRUBBER_CRC_HDR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_LIST_DIR}/tools/system_scrubber_crc_stub.h"
            "${SCRUBBER_CRC_HDR}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/tools/system_scrubber_crc_stub.h"
    COMMENT "Generating system_scrubber_crc_gen.h stub"
    VERBATIM
)

add_library(mini_tree STATIC
    ${OSAL_SRCS}
    ${FREERTOS_SRCS}
    ${HAL_BUS_SRCS}
    ${HAL_SRCS}
    "board/src/board_device.c"
    "board/src/board_dma.c"
    "board/src/bus.c"
    "board/src/dev_lifecycle.c"
    "board/src/board_driver.c"
    "board/src/config_store.c"
    "board/src/task_config.c"
    "board/src/task_utils.c"
    ${DRIVER_SRCS}
    ${GEN_SRCS}
    "core/src/event_bus.c"
    "core/src/buffer_pool.c"
    "core/src/production_log.c"
    "core/src/printf_output.c"
    ${SYSTEM_SRCS}
    "algorithm/buffer/circle_fifo_buffer.c"
)

set_source_files_properties(${GEN_SRCS} PROPERTIES GENERATED TRUE)

target_include_directories(mini_tree PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/bus/dma"
    "${CMAKE_CURRENT_LIST_DIR}/bus/include"
    "${CMAKE_CURRENT_LIST_DIR}/hal/spi"
    "${CMAKE_CURRENT_LIST_DIR}/hal/uart"
    "${CMAKE_CURRENT_LIST_DIR}/bus/spi"
    "${CMAKE_CURRENT_LIST_DIR}/bus/uart"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/spi"
    "${CMAKE_CURRENT_LIST_DIR}/vfs/uart"
)

target_include_directories(mini_tree
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/algorithm/buffer"
        "${CMAKE_CURRENT_LIST_DIR}/board/dt-bindings/gpio"
        "${CMAKE_CURRENT_LIST_DIR}/board/include"
        "${CMAKE_CURRENT_LIST_DIR}/core/include"
        ${HAL_INCLUDE_DIRS}
        "${CMAKE_CURRENT_LIST_DIR}/hal/soc/ch32v307"
        "${CMAKE_CURRENT_LIST_DIR}/osal/include"
        "${CMAKE_CURRENT_LIST_DIR}/system_c/include"
        "${CMAKE_CURRENT_LIST_DIR}/system_cpp/include"
        "${CMAKE_CURRENT_LIST_DIR}/drivers/flash"
        "${CMAKE_CURRENT_LIST_DIR}/drivers/ws2812"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/gpio"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/uart"
        "${KCONFIG_GEN_DIR}"
        "${GENERATED_BOARD_DIR}"
        "${SCRUBBER_GEN_DIR}"
)

if(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_FREERTOS=y")
    target_include_directories(mini_tree PUBLIC
        "${FREERTOS_ROOT}"
        "${FREERTOS_ROOT}/include"
        "${FREERTOS_PORT_DIR}"
    )
endif()

if(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_RTTHREAD=y")
    target_link_libraries(mini_tree PUBLIC rtthread_kernel)
    add_dependencies(mini_tree rtthread_kernel)
endif()

target_compile_definitions(mini_tree PRIVATE ${OSAL_DEFINE})
mini_tree_link_etl(mini_tree)
if(CONFIG_SYSTEM_ENTRY STREQUAL "CONFIG_SYSTEM_CPP=y")
    target_compile_definitions(mini_tree PRIVATE CONFIG_SYSTEM_CPP)
    target_compile_options(mini_tree PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    )
endif()
if(CONFIG_OSAL_ENTRY STREQUAL "CONFIG_OSAL_FREERTOS=y")
    target_compile_definitions(mini_tree PRIVATE
        RISCV_no_extensions
        portasmHAS_MTIME=0
    )
endif()
if(CONFIG_HAL_GPIO_ENTRY STREQUAL "CONFIG_HAL_GPIO_CH32=y")
    target_include_directories(mini_tree PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/../SRC/Peripheral/inc"
        "${CMAKE_CURRENT_LIST_DIR}/../SRC/Core"
        "${CMAKE_CURRENT_LIST_DIR}/../SRC/User"
    )
    target_link_libraries(mini_tree PUBLIC ch32_periph)
endif()
add_custom_target(mini_tree_gen DEPENDS "${KCONFIG_OUT}" ${GEN_SRCS} ${GEN_HDRS} "${SCRUBBER_CRC_HDR}")
add_dependencies(mini_tree mini_tree_gen)
