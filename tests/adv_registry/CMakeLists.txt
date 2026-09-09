cmake_minimum_required(VERSION 3.16)
project(adv_registry_unit C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(adv_registry_unit
    test_adv_apps.c
    ../../platform/adv/adv_apps.c
)

target_include_directories(adv_registry_unit PRIVATE
    ../../include
    ../../core
    ../../core/minishell_services
)

target_compile_options(adv_registry_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)

enable_testing()
add_test(NAME adv_static_registry_unit COMMAND adv_registry_unit)
