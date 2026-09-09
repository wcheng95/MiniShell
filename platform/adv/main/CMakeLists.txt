get_filename_component(MINISHELL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

set(MINISHELL_SERVICE_SOURCES
    "${MINISHELL_ROOT}/core/minishell_services/services.c"
    "${MINISHELL_ROOT}/core/minishell_services/system_service.c"
    "${MINISHELL_ROOT}/core/minishell_services/memory_service.c"
    "${MINISHELL_ROOT}/core/minishell_services/filesystem_service.c"
    "${MINISHELL_ROOT}/core/minishell_services/filesystem_paths.c"
    "${MINISHELL_ROOT}/core/minishell_services/filesystem_handles.c"
    "${MINISHELL_ROOT}/core/minishell_services/filesystem_quota.c"
    "${MINISHELL_ROOT}/core/minishell_services/time_location_service.c"
    "${MINISHELL_ROOT}/core/minishell_services/display_service.c"
    "${MINISHELL_ROOT}/core/minishell_services/input_service.c"
    "${MINISHELL_ROOT}/core/minishell_services/audio_service.c"
)

idf_component_register(
    SRCS
        "app_main.c"
        "hello_static.c"
        "probe_static.c"
        "a3_probe_static.c"
        "date_static.c"
        "${MINISHELL_ROOT}/core/minishell_runtime.c"
        "${MINISHELL_ROOT}/core/shell.c"
        "${MINISHELL_ROOT}/core/app_manager.c"
        ${MINISHELL_SERVICE_SOURCES}
        "${CMAKE_CURRENT_LIST_DIR}/../adv_backend.c"
        "${CMAKE_CURRENT_LIST_DIR}/../adv_console.c"
        "${CMAKE_CURRENT_LIST_DIR}/../adv_display.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../adv_filesystem.c"
        "${CMAKE_CURRENT_LIST_DIR}/../adv_i2c.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../adv_input.c"
        "${CMAKE_CURRENT_LIST_DIR}/../adv_keyboard.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../adv_memory.c"
        "${CMAKE_CURRENT_LIST_DIR}/../adv_time_location.c"
        "${CMAKE_CURRENT_LIST_DIR}/../adv_apps.c"
        "${CMAKE_CURRENT_LIST_DIR}/../platform_info.c"
        "${CMAKE_CURRENT_LIST_DIR}/../resource_config.c"
    INCLUDE_DIRS
        "${MINISHELL_ROOT}/include"
        "${MINISHELL_ROOT}/core"
        "${MINISHELL_ROOT}/core/minishell_services"
        "${CMAKE_CURRENT_LIST_DIR}/.."
    REQUIRES
        esp_driver_i2c
        esp_driver_usb_serial_jtag
        esp_timer
        littlefs
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wpedantic)
