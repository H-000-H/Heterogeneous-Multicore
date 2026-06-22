get_filename_component(_HAL_BUS_ROOT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(HAL_BUS_INCLUDE_DIRS
    "${_HAL_BUS_ROOT}/spi"
    "${_HAL_BUS_ROOT}/i2c"
    "${_HAL_BUS_ROOT}/can"
    "${_HAL_BUS_ROOT}/i2s"
    "${_HAL_BUS_ROOT}/pcie"
    "${_HAL_BUS_ROOT}/usb"
)
