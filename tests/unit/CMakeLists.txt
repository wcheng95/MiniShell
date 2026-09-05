cmake_minimum_required(VERSION 3.16)
project(minishell_unit_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(minishell_unit_tests
    ../../core/minishell_services/services.c
    ../../core/minishell_services/system_service.c
    ../../core/minishell_services/memory_service.c
    ../../core/minishell_services/filesystem_service.c
    ../../core/minishell_services/time_location_service.c
    ../../core/minishell_services/display_service.c
    ../../core/minishell_services/input_service.c
    ../../core/minishell_transfer/transfer.c
    ../../core/minishell_power/power.c
    test_support.c
    test_system.c
    test_memory.c
    test_filesystem.c
    test_time_location.c
    test_display.c
    test_input.c
    test_transfer.c
    test_power.c
    test_main.c
)

target_include_directories(minishell_unit_tests PRIVATE
    ../../include
    ../../core/minishell_services
    ../../core/minishell_transfer
    ../../core/minishell_power
)

target_compile_options(minishell_unit_tests PRIVATE -Wall -Wextra -Werror)

enable_testing()
foreach(group system memory filesystem time_location display input transfer)
    add_test(NAME abi_${group}_unit COMMAND minishell_unit_tests ${group})
endforeach()
add_test(NAME resident_power_unit COMMAND minishell_unit_tests power)
