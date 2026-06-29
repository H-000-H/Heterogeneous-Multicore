# HAL 子目录 include 路径
get_filename_component(_HAL_ROOT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(HAL_INCLUDE_DIRS
    "${_HAL_ROOT}/gpio"
    "${_HAL_ROOT}/cpu"
    "${_HAL_ROOT}/pwm"
    "${_HAL_ROOT}/analog"
    "${_HAL_ROOT}/storage"
    "${_HAL_ROOT}/system"
    "${_HAL_ROOT}/soc/ch32v307"
)
