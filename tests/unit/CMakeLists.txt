cmake_minimum_required(VERSION 3.16)
project(minishell_unit_tests C)

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
    test_nano_buffer.c
    test_cp_copy.c
    test_main.c
)

target_include_directories(minishell_unit_tests PRIVATE
    ../../include
    ../../core/minishell_services
    ../../apps/nano/main
    ../../apps/cp/main
)

target_compile_options(minishell_unit_tests PRIVATE -Wall -Wextra -Werror)

add_executable(keyer_engine_k3_unit
    ../keyer_engine_k3_test.c
    ../../apps/keyer/src/keyer_engine/keyer_engine.c
    ../../apps/keyer/src/keyer_engine/keyer_decoder.c
)
target_include_directories(keyer_engine_k3_unit PRIVATE
    ../../apps/keyer/src/keyer_engine
)
target_compile_options(keyer_engine_k3_unit PRIVATE
    -Wall -Wextra -Werror -Wpedantic
)

enable_testing()
foreach(group system console memory filesystem time_location display input audio)
    add_test(NAME api_${group}_unit COMMAND minishell_unit_tests ${group})
endforeach()
add_test(NAME nano_editor_unit COMMAND minishell_unit_tests nano_buffer)
add_test(NAME cp_copy_unit COMMAND minishell_unit_tests cp_copy)
add_test(NAME keyer_engine_k3_unit COMMAND keyer_engine_k3_unit)
