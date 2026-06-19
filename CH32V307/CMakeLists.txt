cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_GENERATOR)
    set(CMAKE_GENERATOR "Ninja" CACHE INTERNAL "" FORCE)
endif()

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/riscv32-wch-elf-ch32v307.cmake"
        CACHE FILEPATH "Toolchain file")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# 默认 Debug 模式
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_PROJECT_NAME "ch307_node")
message(STATUS "CMAKE_PROJECT_NAME: ${CMAKE_PROJECT_NAME}")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(${CMAKE_PROJECT_NAME} VERSION 1.0.0 LANGUAGES C CXX ASM)

file(GLOB_RECURSE CH307_NODE_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Core/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/SRC/User/ch32v30x_it.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Startup/startup_ch32v30x_D8C.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/src/*.cpp"
)

set(CH307_NODE_HDR_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Peripheral/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Core"
    "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Debug"
    "${CMAKE_CURRENT_SOURCE_DIR}/SRC/User"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/inc"
)

set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Ld/Link.ld")

# Link.ld: FLASH 288K + RAM 32K (CH32V307 D8C default layout)
set(CH307_FLASH_BYTES 294912)
set(CH307_RAM_BYTES 32768)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(CH307_SIZE_TOOL
    NAMES riscv32-wch-elf-size riscv32-esp-elf-size riscv-none-elf-size
    HINTS
        "${CH32_RISCV_TOOLCHAIN_DIR}/bin"
        "D:/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC15/bin"
)

add_executable(${CMAKE_PROJECT_NAME})

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${CH307_NODE_SRCS})

add_subdirectory(mini_tree)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE mini_tree)

target_include_directories(mini_tree PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Peripheral/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Core"
    "${CMAKE_CURRENT_SOURCE_DIR}/SRC/Debug"
    "${CMAKE_CURRENT_SOURCE_DIR}/SRC/User"
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CH307_NODE_HDR_DIRS})

list(REMOVE_ITEM CMAKE_CXX_STANDARD_LIBRARIES "stdc++")
if(NOT "mini_tree" STREQUAL CMAKE_PROJECT_NAME)
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
        -march=rv32imac_zicsr_zifencei -mabi=ilp32 -mcmodel=medany
        -msmall-data-limit=8 -mno-save-restore
        $<$<CONFIG:Debug>:-Og -ggdb>
        $<$<CONFIG:Release>:-Os -DNDEBUG>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    )
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
        -T "${LINKER_SCRIPT}"
        -T "${CMAKE_CURRENT_SOURCE_DIR}/mini_tree/error_symbols.ld"
        -nostartfiles
        --specs=nosys.specs
        --specs=nano.specs
        -Wl,-Map=${CMAKE_PROJECT_NAME}.map,--gc-sections
    )

    if(CH307_SIZE_TOOL)
        add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            WORKING_DIRECTORY $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>
            COMMAND ${CMAKE_COMMAND} -E echo ""
            COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
            COMMAND ${CMAKE_COMMAND} -E echo "         CH307 Firmware Memory Usage"
            COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
            COMMAND "${CH307_SIZE_TOOL}" --format=berkeley $<TARGET_FILE_NAME:${CMAKE_PROJECT_NAME}>
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/firmware_size_report.py"
                --size-tool "${CH307_SIZE_TOOL}"
                --elf "$<TARGET_FILE_NAME:${CMAKE_PROJECT_NAME}>"
                --flash-bytes ${CH307_FLASH_BYTES}
                --ram-bytes ${CH307_RAM_BYTES}
            COMMENT "Reporting firmware memory usage..."
            VERBATIM
        )
    else()
        message(WARNING "riscv32-wch-elf-size not found; skip size report.")
    endif()

    include("${CMAKE_CURRENT_SOURCE_DIR}/mini_tree/cmake/disasm.cmake")
    mini_tree_add_disasm_postbuild(
        "${CMAKE_CURRENT_SOURCE_DIR}/mini_tree/.config"
        ${CMAKE_PROJECT_NAME}
    )
endif()