get_filename_component(_VFS_SPI_ROOT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(VFS_SPI_INCLUDE_DIRS
    "${_VFS_SPI_ROOT}/include"
    "${_VFS_SPI_ROOT}/master"
    "${_VFS_SPI_ROOT}/slave"
)
