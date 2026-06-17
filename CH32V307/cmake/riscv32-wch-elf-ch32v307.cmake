# CH32V307 — WCH (沁恒/MounRiver) RISC-V 工具链
#
# 默认使用 MounRiver Studio 自带:
#   riscv32-wch-elf-gcc
#
# 覆盖方式 (任选其一):
#   cmake -B build -DCH32_RISCV_TOOLCHAIN_DIR="D:/path/to/RISC-V Embedded GCC15"
#   或将 .../bin 加入 PATH (前缀 riscv32-wch-elf-)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)

if(NOT DEFINED CH32_RISCV_TOOLCHAIN_DIR)
    set(_wch_candidates
        "$ENV{CH32_RISCV_TOOLCHAIN_DIR}"
        "D:/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC15"
        "C:/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC15"
        "D:/MounRiver/MounRiver_Studio/toolchain/RISC-V Embedded GCC"
        "C:/MounRiver/MounRiver_Studio/toolchain/RISC-V Embedded GCC"
    )
    foreach(_candidate IN LISTS _wch_candidates)
        if(_candidate AND EXISTS "${_candidate}/bin/riscv32-wch-elf-gcc.exe")
            set(CH32_RISCV_TOOLCHAIN_DIR "${_candidate}" CACHE PATH "WCH RISC-V toolchain root")
            break()
        endif()
    endforeach()
    unset(_wch_candidates)
    unset(_candidate)
endif()

if(CH32_RISCV_TOOLCHAIN_DIR)
    set(_wch_bin "${CH32_RISCV_TOOLCHAIN_DIR}/bin")
    set(CMAKE_C_COMPILER   "${_wch_bin}/riscv32-wch-elf-gcc.exe")
    set(CMAKE_CXX_COMPILER "${_wch_bin}/riscv32-wch-elf-g++.exe")
    set(CMAKE_ASM_COMPILER "${_wch_bin}/riscv32-wch-elf-gcc.exe")
    set(CMAKE_OBJCOPY      "${_wch_bin}/riscv32-wch-elf-objcopy.exe")
    set(CMAKE_SIZE         "${_wch_bin}/riscv32-wch-elf-size.exe")
    set(CMAKE_OBJDUMP      "${_wch_bin}/riscv32-wch-elf-objdump.exe")
    set(CMAKE_GDB          "${_wch_bin}/riscv32-wch-elf-gdb.exe")
    unset(_wch_bin)
    message(STATUS "CH32 toolchain: ${CH32_RISCV_TOOLCHAIN_DIR} (riscv32-wch-elf)")
else()
    set(CMAKE_C_COMPILER   riscv32-wch-elf-gcc)
    set(CMAKE_CXX_COMPILER riscv32-wch-elf-g++)
    set(CMAKE_ASM_COMPILER riscv32-wch-elf-gcc)
    set(CMAKE_OBJCOPY      riscv32-wch-elf-objcopy)
    set(CMAKE_SIZE         riscv32-wch-elf-size)
    set(CMAKE_OBJDUMP      riscv32-wch-elf-objdump)
    set(CMAKE_GDB          riscv32-wch-elf-gdb)
    message(STATUS "CH32 toolchain: riscv32-wch-elf from PATH")
endif()

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
