cmake_minimum_required(VERSION 3.16)
project(minishell_unit_tests C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(minishell_unit_tests
    ../../core/minishell_services/services.c
    ../../core/minishell_services/system_service.c
    ../../core/minishell_services/console_service.c
    ../../core/minishell_services/memory_service.c
    ../../core/minishell_services/filesystem_service.c
    ../../core/minishell_services/filesystem_paths.c
    ../../core/minishell_services/filesystem_handles.c
    ../../core/minishell_services/filesystem_quota.c
    ../../core/minishell_services/time_location_service.c
    ../../core/minishell_services/display_service.c
    ../../core/minishell_services/input_service.c
    ../../core/minishell_services/audio_service.c
    ../../core/minishell_services/digital_io_service.c
    ../../core/minishell_services/serial_service.c
    ../../apps/nano/main/nano_buffer.c
    ../../apps/cp/main/cp_copy.c
    test_support.c
    test_system.c
    test_console.c
    test_memory.c
    test_filesystem.c
    test_time_location.c
    test_display.c
    test_input.c
    test_audio.c
    test_serial.c
    test_nano_buffer.c
    test_cp_copy.c
    test_mv.c
    test_main.c
)

target_include_directories(minishell_unit_tests PRIVATE
    ../../include
    ../../core/minishell_services
    ../../apps/nano/main
    ../../apps/cp/main
)

target_compile_options(minishell_unit_tests PRIVATE -Wall -Wextra -Werror)

include(../minicw_tests.cmake)

add_test(NAME minishell_unit_tests COMMAND minishell_unit_tests)
include(../js8_tests.cmake)
