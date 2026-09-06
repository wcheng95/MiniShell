cmake_minimum_required(VERSION 3.16)
project(MiniShell VERSION 0.1.0 LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "MiniShell reference build targets Linux. "
        "This build directory appears to contain a non-Linux toolchain cache. "
        "Use a fresh build directory, for example: cmake -S . -B build-linux")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(MINISHELL_SERVICE_SOURCES
    core/minishell_services/services.c
    core/minishell_services/system_service.c
    core/minishell_services/memory_service.c
    core/minishell_services/filesystem_service.c
    core/minishell_services/time_location_service.c
    core/minishell_services/display_service.c
    core/minishell_services/input_service.c
)

add_executable(minishell
    core/main.c
    core/shell.c
    core/app_manager.c
    ${MINISHELL_SERVICE_SOURCES}
    platform/linux/linux_backend.c
)

target_include_directories(minishell PRIVATE
    include
    core
    core/minishell_services
)

target_link_libraries(minishell PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(minishell PROPERTIES ENABLE_EXPORTS ON)
target_compile_options(minishell PRIVATE -Wall -Wextra -Wpedantic)

add_library(hello MODULE apps/hello/hello.c)
target_include_directories(hello PRIVATE include)
set_target_properties(hello PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hello"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime/apps"
)
target_compile_options(hello PRIVATE -Wall -Wextra -Wpedantic)

add_library(service_probe MODULE tests/apps/service_probe.c)
target_include_directories(service_probe PRIVATE include)
set_target_properties(service_probe PROPERTIES
    PREFIX ""
    OUTPUT_NAME "service_probe"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-apps"
)
target_compile_options(service_probe PRIVATE -Wall -Wextra -Wpedantic)

add_library(input_probe MODULE tests/apps/input_probe.c)
target_include_directories(input_probe PRIVATE include)
set_target_properties(input_probe PROPERTIES
    PREFIX ""
    OUTPUT_NAME "input_probe"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-apps"
)
target_compile_options(input_probe PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(
    NAME linux_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_smoke.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:hello>
)

add_test(
    NAME linux_services
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_services.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:service_probe>
)

add_test(
    NAME linux_input
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_input.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:input_probe>
)
