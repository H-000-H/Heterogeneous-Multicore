# POST_BUILD disassembly listing when mini_tree .config has CONFIG_BUILD_DISASM=y

function(mini_tree_add_disasm_postbuild kconfig_dot target)
    if(NOT TARGET ${target})
        message(WARNING "mini_tree_add_disasm_postbuild: target '${target}' not found")
        return()
    endif()
    if(NOT EXISTS "${kconfig_dot}")
        return()
    endif()
    file(STRINGS "${kconfig_dot}" _disasm_entry REGEX "^CONFIG_BUILD_DISASM=y$")
    if(NOT _disasm_entry STREQUAL "CONFIG_BUILD_DISASM=y")
        return()
    endif()

    set(_objdump "")
    foreach(_compiler IN ITEMS CMAKE_C_COMPILER CMAKE_CXX_COMPILER)
        if(NOT _objdump AND ${${_compiler}} MATCHES "xtensa|esp-elf")
            get_filename_component(_compiler_dir "${${_compiler}}" DIRECTORY)
            get_filename_component(_compiler_name "${${_compiler}}" NAME)
            string(REGEX REPLACE "g\\+\\+.*$" "objdump" _objdump_name "${_compiler_name}")
            string(REGEX REPLACE "gcc.*$" "objdump" _objdump_name "${_objdump_name}")
            set(_toolchain_objdump "${_compiler_dir}/${_objdump_name}")
            if(EXISTS "${_toolchain_objdump}")
                set(_objdump "${_toolchain_objdump}")
            endif()
        endif()
    endforeach()
    if(NOT _objdump AND CMAKE_OBJDUMP MATCHES "xtensa|esp-elf")
        set(_objdump "${CMAKE_OBJDUMP}")
    endif()
    if(NOT _objdump)
        find_program(_objdump NAMES xtensa-esp32s3-elf-objdump xtensa-esp-elf-objdump)
    endif()
    if(NOT _objdump)
        message(WARNING "CONFIG_BUILD_DISASM=y but Xtensa objdump not found")
        return()
    endif()
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        WORKING_DIRECTORY $<TARGET_FILE_DIR:${target}>
        COMMAND ${_objdump} -d -S $<TARGET_FILE_NAME:${target}>
                > $<TARGET_FILE_BASE_NAME:${target}>.lst
        COMMENT "Generating disassembly listing (.lst)"
    )
endfunction()
