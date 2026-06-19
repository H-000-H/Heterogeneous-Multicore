set(APP_SRCS

    "${CMAKE_CURRENT_SOURCE_DIR}/src/app_freertos.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app_led_task.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app_spi_task.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app_flash_task.cpp"

)

set(APP_INCLUDE_DIRS

    "${CMAKE_CURRENT_SOURCE_DIR}/inc"

)

idf_component_register(

    SRCS

        ${APP_SRCS}

    INCLUDE_DIRS

        ${APP_INCLUDE_DIRS}

    REQUIRES

        mini_tree hal driver nvs_flash

)

