cmake_minimum_required(VERSION 3.22)

# 1. 优先锁定 Ninja 构建器，保留手动覆盖能力
if(NOT CMAKE_GENERATOR)
    set(CMAKE_GENERATOR "Ninja" CACHE INTERNAL "" FORCE)
endif()

# 2. 工具链文件强制前置加载
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/riscv32-wch-elf-ch32v307.cmake"
            CACHE FILEPATH "RISC-V WCH 交叉工具链配置")
endif()

# 3. C/C++17 标准，启用 GCC 扩展语法
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# 默认构建类型 Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type: Debug/Release")
endif()

# 工程定义，支持 C/C++/汇编
set(CMAKE_PROJECT_NAME "ch307_node")
project(${CMAKE_PROJECT_NAME} VERSION 1.0.0 LANGUAGES C CXX ASM)

# 生成 compile_commands.json 适配 Clangd/VSCode
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 4. 检索交叉工具链 size / objcopy
#    优先复用工具链文件已三端自动探测的 CMAKE_SIZE / CMAKE_OBJCOPY，避免重复查找
set(_CH32_RISCV_BINDIR_HINTS
        "${CH32_RISCV_TOOLCHAIN_DIR}/bin"
        "/opt/toolchains/riscv/bin"
        "/opt/riscv/bin"
        "/opt/MounRiver/MounRiver_Studio/toolchain/riscv64-musl-1.0.3/bin"
        "/usr" "/usr/local"
        "C:/MounRiver/MounRiver_Studio/toolchain/riscv64-musl-1.0.3/bin"
        "C:/Program Files/MounRiver/MounRiver_Studio/toolchain")

if(CMAKE_SIZE)
    set(CH307_SIZE_TOOL "${CMAKE_SIZE}" CACHE FILEPATH "RISC-V size 工具")
else()
    find_program(CH307_SIZE_TOOL
            NAMES riscv32-wch-elf-size riscv-none-elf-size riscv-none-embed-size size
            HINTS ${_CH32_RISCV_BINDIR_HINTS})
endif()

if(CMAKE_OBJCOPY)
    set(RISCV_OBJCOPY "${CMAKE_OBJCOPY}" CACHE FILEPATH "RISC-V objcopy 工具")
else()
    find_program(RISCV_OBJCOPY
            NAMES riscv32-wch-elf-objcopy riscv-none-elf-objcopy riscv-none-embed-objcopy
            HINTS ${_CH32_RISCV_BINDIR_HINTS})
endif()

# 5. 【优化点】移除 GLOB_RECURSE，手动显式管理源码，规避缓存失效问题
# 5. 【精准对齐】显式管理源码，规避缓存失效与 GLOB 乱序问题
set(CH307_NODE_SRCS
        # Core 硬件支持
        "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Core/system_ch32v30x.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Core/core_riscv.c"

        # Debug 辅助
        "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Debug/debug.c"

        # User 硬件中断与基础板载外设配置
        "${CMAKE_CURRENT_SOURCE_DIR}/SRC/User/ch32v30x_it.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/SRC/User/usart.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/SRC/User/spi.c"

        # 启动汇编文件
        "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Startup/startup_ch32v30x_D8C.S"

        # C++ 核心应用层与 ETL 任务总线
        "${CMAKE_CURRENT_SOURCE_DIR}/app/src/main.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/app/src/freeRTOS.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/app/src/app_spi_task.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/app/src/app_cmd_handlers.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/app/src/app_led_task.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/app/src/app_flash_task.cpp"
)
# 全局头文件目录
set(CH307_NODE_HDR_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Peripheral/inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Core"
        "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Debug"
        "${CMAKE_CURRENT_SOURCE_DIR}/SRC/User"
        "${CMAKE_CURRENT_SOURCE_DIR}/app/inc"
)

# 链接脚本与芯片容量定义
set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Ld/Link.ld")
set(CH307_FLASH_BYTES 294912)
set(CH307_RAM_BYTES 32768)

# 6. 构建主可执行目标
add_executable(${CMAKE_PROJECT_NAME} ${CH307_NODE_SRCS})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CH307_NODE_HDR_DIRS})

# 7. 加载底层HAL与业务框架子模块
add_subdirectory(hal)
add_subdirectory(mini_tree)

# 链接依赖库
target_link_libraries(${CMAKE_PROJECT_NAME}
        PRIVATE
        mini_tree
        ch32_periph
        nosys
        supc++
)
# mini_tree 需要上层全局头文件
target_include_directories(mini_tree PRIVATE ${CH307_NODE_HDR_DIRS})

# 全局移除完整 libstdc++，强制使用轻量 supc++
list(REMOVE_ITEM CMAKE_CXX_STANDARD_LIBRARIES "stdc++")

# 8. 编译参数合集
## RISC-V 硬件架构参数（完美向后兼容 GCC 8.2.0 老战神）
set(CH307_ARCH_OPTS
        -march=rv32imac
        -mabi=ilp32
        -mcmodel=medany
        -msmall-data-limit=8
        -mno-save-restore
)

## 全局警告检查（新增优化项）
set(GLOBAL_WARN_OPTS
        -Wall
        -Wextra
        -Wshadow
        -Wundef
        -Wconversion
        -Wno-unused-parameter
        -Wno-unused-variable
)

## 分模式优化 + C++ 裁剪选项
set(CH307_BUILD_OPTS
        $<$<CONFIG:Debug>:-Og -ggdb3>
        $<$<CONFIG:Release>:-Os -DNDEBUG>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
)

# 合并全部编译选项
set(CH307_COMPILE_OPTIONS
        ${CH307_ARCH_OPTS}
        ${GLOBAL_WARN_OPTS}
        ${CH307_BUILD_OPTS}
)

# 全模块统一编译参数，保证ABI一致
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE ${CH307_COMPILE_OPTIONS})
target_compile_options(ch32_periph PRIVATE ${CH307_COMPILE_OPTIONS})
target_compile_options(mini_tree PRIVATE ${CH307_COMPILE_OPTIONS})

# 9. 链接参数配置
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
        -T "${LINKER_SCRIPT}"
        -T "${CMAKE_CURRENT_SOURCE_DIR}/mini_tree/error_symbols.ld"
        -nostartfiles
        --specs=nosys.specs
        --specs=nano.specs
        -Wl,-Map=${CMAKE_PROJECT_NAME}.map,--gc-sections,--print-memory-usage
)

# =================================================================
# 10. 后置构建流水线
# =================================================================
## 10.1 自动生成 .bin / .hex 固件文件（新增优化项）
if(RISCV_OBJCOPY)
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            WORKING_DIRECTORY $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>
            COMMAND ${RISCV_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
            COMMAND ${RISCV_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
            COMMENT "Generate firmware binary & hex image"
            VERBATIM
    )
endif()

## 10.2 打印内存占用报表 + Python 容量统计
if(CH307_SIZE_TOOL)
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            WORKING_DIRECTORY $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>
            COMMAND ${CMAKE_COMMAND} -E echo ""
            COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
            COMMAND ${CMAKE_COMMAND} -E echo "         CH32V307 Firmware Memory Usage"
            COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
            COMMAND "${CH307_SIZE_TOOL}" --format=berkeley $<TARGET_FILE_NAME:${CMAKE_PROJECT_NAME}>
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/firmware_size_report.py"
            --size-tool "${CH307_SIZE_TOOL}"
            --elf "$<TARGET_FILE_NAME:${CMAKE_PROJECT_NAME}>"
            --flash-bytes ${CH307_FLASH_BYTES}
            --ram-bytes ${CH307_RAM_BYTES}
            COMMENT "Calculate flash & ram remaining capacity"
            VERBATIM
    )
endif()

## 10.3 mini_tree 自动反汇编导出
include("${CMAKE_CURRENT_SOURCE_DIR}/mini_tree/cmake/disasm.cmake")
mini_tree_add_disasm_postbuild(
        "${CMAKE_CURRENT_SOURCE_DIR}/mini_tree/.config"
        ${CMAKE_PROJECT_NAME}
)