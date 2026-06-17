# mini_tree (generic CMake build for non-ESP-IDF projects)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(KCONFIG_DOT "${CMAKE_CURRENT_LIST_DIR}/.config")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${KCONFIG_DOT}")

file(STRINGS "${KCONFIG_DOT}" CONFIG_OSAL_ENTRY   REGEX "^CONFIG_OSAL_[A-Z_]+=y$")
file(STRINGS "${KCONFIG_DOT}" CONFIG_SYSTEM_ENTRY REGEX "^CONFIG_SYSTEM_(C|CPP)=y$")

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

set(KCONFIG_GEN_DIR "${CMAKE_BINARY_DIR}/generated/kconfig/mini_tree")
set(KCONFIG_OUT     "${KCONFIG_GEN_DIR}/config.h")

set(GENERATED_BOARD_DIR "${CMAKE_BINARY_DIR}/generated/board/mini_tree")
set(SCRUBBER_GEN_DIR    "${CMAKE_BINARY_DIR}/generated/scrubber/mini_tree")
set(SCRUBBER_CRC_HDR    "${SCRUBBER_GEN_DIR}/system_scrubber_crc_gen.h")
set(BOARD_DTS           "${CMAKE_CURRENT_LIST_DIR}/board/dts/ch32v307.dts")
set(BOARD_DTSI_DIR      "${CMAKE_CURRENT_LIST_DIR}/board/dtsi")
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
            "${CMAKE_CURRENT_LIST_DIR}/drivers"
    DEPENDS
        "${DTC_LITE}"
        "${BOARD_DTS}"
        "${BOARD_DTSI_DIR}/ch32v307.dtsi"
        "${BOARD_DTSI_DIR}/ch32v307-spi.dtsi"
        "${CMAKE_CURRENT_LIST_DIR}/board/dt-bindings/spi/spi-parameter.h"
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
    "hal_bus/src/hal_bus_dummy.c"
    "hal_inst/src/hal_inst_dummy.c"
    "hal_if/src/hal_cpu_amp.c"
    "hal_if/src/hal_ch32v307.c"
    "hal_if/src/hal_platform_safety.c"
    "board/src/board_device.c"
    "board/src/dev_lifecycle.c"
    "board/src/board_driver.c"
    "board/src/config_store.c"
    "board/src/task_config.c"
    "board/src/task_utils.c"
    ${GEN_SRCS}
    "core/src/event_bus.c"
    "core/src/buffer_pool.c"
    "core/src/production_log.c"
    "core/src/printf_output.c"
    ${SYSTEM_SRCS}
    "algorithm/buffer/circle_fifo_buffer.c"
)

set_source_files_properties(${GEN_SRCS} PROPERTIES GENERATED TRUE)

target_include_directories(mini_tree
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/algorithm/buffer"
        "${CMAKE_CURRENT_LIST_DIR}/board/include"
        "${CMAKE_CURRENT_LIST_DIR}/compat"
        "${CMAKE_CURRENT_LIST_DIR}/core/include"
        "${CMAKE_CURRENT_LIST_DIR}/display_if/include"
        "${CMAKE_CURRENT_LIST_DIR}/hal_bus/include"
        "${CMAKE_CURRENT_LIST_DIR}/hal_inst/include"
        "${CMAKE_CURRENT_LIST_DIR}/hal_if/include"
        "${CMAKE_CURRENT_LIST_DIR}/vfs/spi"
        "${CMAKE_CURRENT_LIST_DIR}/osal/include"
        "${CMAKE_CURRENT_LIST_DIR}/system_c/include"
        "${CMAKE_CURRENT_LIST_DIR}/system_cpp/include"
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
add_custom_target(mini_tree_gen DEPENDS "${KCONFIG_OUT}" ${GEN_SRCS} ${GEN_HDRS} "${SCRUBBER_CRC_HDR}")
add_dependencies(mini_tree mini_tree_gen)
