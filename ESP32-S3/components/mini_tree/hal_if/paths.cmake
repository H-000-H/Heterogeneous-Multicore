# hal_if 子目录 include 路径 (供 core/system 等待 PRIVATE 引用)
get_filename_component(_HAL_IF_ROOT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(HAL_IF_INCLUDE_DIRS
    "${_HAL_IF_ROOT}/gpio"
    "${_HAL_IF_ROOT}/cpu"
    "${_HAL_IF_ROOT}/uart"
    "${_HAL_IF_ROOT}/pwm"
    "${_HAL_IF_ROOT}/pulse"
    "${_HAL_IF_ROOT}/analog"
    "${_HAL_IF_ROOT}/storage"
    "${_HAL_IF_ROOT}/system"
)
