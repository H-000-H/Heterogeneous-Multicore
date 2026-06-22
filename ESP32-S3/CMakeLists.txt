# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH} OR "$ENV{IDF_PATH}" STREQUAL "")
  message(FATAL_ERROR
    "IDF_PATH is not set. Configure via ESP-IDF extension, CMakePresets.json "
    "(preset: esp-idf), or run export.ps1 / export.bat before cmake.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mini-tree-rt-treadtest)

# GCC 14.2 (-Og) ICE on esp_lcd/rgb/esp_lcd_panel_rgb.c (RTL pass: ira).
# Keep project at Debug; only raise optimization for this IDF component.
idf_component_get_property(_esp_lcd_lib esp_lcd COMPONENT_LIB)
target_compile_options(${_esp_lcd_lib} PRIVATE -O2)

include(${CMAKE_SOURCE_DIR}/components/mini_tree/cmake/disasm.cmake)
cmake_language(DEFER CALL mini_tree_add_disasm_postbuild
    "${CMAKE_SOURCE_DIR}/components/mini_tree/.config"
    "${CMAKE_PROJECT_NAME}.elf"
)
