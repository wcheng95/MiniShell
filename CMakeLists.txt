cmake_minimum_required(VERSION 3.16)
project(minishell VERSION 0.1.0 LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "minishell reference build targets Linux. "
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
    core/minishell_services/filesystem_paths.c
    core/minishell_services/filesystem_handles.c
    core/minishell_services/filesystem_quota.c
    core/minishell_services/time_location_service.c
    core/minishell_services/display_service.c
    core/minishell_services/input_service.c
    core/minishell_services/audio_service.c
)

set(MINISHELL_LINUX_SOURCES
    platform/linux/linux_backend.c
    platform/linux/linux_common.c
    platform/linux/linux_filesystem.c
    platform/linux/linux_time_location.c
    platform/linux/linux_terminal.c
    platform/linux/linux_terminal_parser.c
    platform/linux/linux_loader.c
    platform/linux/linux_services.c
    platform/linux/linux_audio_wav.c
    platform/linux/platform_info.c
    platform/linux/resource_config.c
)

add_executable(minishell
    core/main.c
    core/shell.c
    core/app_manager.c
    ${MINISHELL_SERVICE_SOURCES}
    ${MINISHELL_LINUX_SOURCES}
)

target_include_directories(minishell PRIVATE
    include
    core
    core/minishell_services
    platform/linux
)

target_link_libraries(minishell PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(minishell PROPERTIES ENABLE_EXPORTS ON)
target_compile_options(minishell PRIVATE -Wall -Wextra -Wpedantic)

function(add_minishell_app target)
    add_library(${target} MODULE ${ARGN})
    target_include_directories(${target} PRIVATE
        include
        apps/${target}/main
    )
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${target}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime/apps"
    )
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

add_minishell_app(hello apps/hello/hello.c)
add_minishell_app(cat apps/cat/main/cat.c)
add_minishell_app(cp
    apps/cp/main/cp.c
    apps/cp/main/cp_copy.c
)
add_minishell_app(date apps/date/main/date.c)
add_minishell_app(df apps/df/main/df.c)
add_minishell_app(free apps/free/main/free.c)
add_minishell_app(ls apps/ls/main/ls.c)
add_minishell_app(mkdir apps/mkdir/main/mkdir.c)
add_minishell_app(mv apps/mv/main/mv.c)
add_minishell_app(rm apps/rm/main/rm.c)
add_minishell_app(rmdir apps/rmdir/main/rmdir.c)
add_minishell_app(nano
    apps/nano/main/nano.c
    apps/nano/main/nano_buffer.c
    apps/nano/main/nano_file.c
    apps/nano/main/nano_ui.c
    apps/nano/main/nano_util.c
)

add_minishell_app(minift8
    apps/minift8/main/minift8_main.c
    apps/minift8/main/minishell_ui_adapter.c
    apps/minift8/src/app_controller/app_controller.c
    apps/minift8/src/config_service/config_service.c
    apps/minift8/src/qso_scheduler/qso_scheduler.c
    apps/minift8/src/storage_service/storage_service.c
    apps/minift8/src/ui_shell/ui_shell.c
)
target_include_directories(minift8 PRIVATE
    apps/minift8/include
    apps/minift8/src/app_controller
    apps/minift8/src/config_service
    apps/minift8/src/qso_scheduler
    apps/minift8/src/storage_service
    apps/minift8/src/ui_shell
)

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

add_library(dir_probe MODULE tests/apps/dir_probe.c)
target_include_directories(dir_probe PRIVATE include)
set_target_properties(dir_probe PROPERTIES
    PREFIX ""
    OUTPUT_NAME "dir_probe"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-apps"
)
target_compile_options(dir_probe PRIVATE -Wall -Wextra -Wpedantic)

add_library(resource_probe MODULE tests/apps/resource_probe.c)
target_include_directories(resource_probe PRIVATE include)
set_target_properties(resource_probe PROPERTIES
    PREFIX ""
    OUTPUT_NAME "resource_probe"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-apps"
)
target_compile_options(resource_probe PRIVATE -Wall -Wextra -Wpedantic)

add_library(audio_probe MODULE tests/apps/audio_probe.c)
target_include_directories(audio_probe PRIVATE include)
set_target_properties(audio_probe PROPERTIES
    PREFIX ""
    OUTPUT_NAME "audio_probe"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-apps"
)
target_compile_options(audio_probe PRIVATE -Wall -Wextra -Wpedantic)

add_executable(minift8_ui_smoke
    tests/minift8_ui_smoke.c
    apps/minift8/src/ui_shell/ui_shell.c
)
target_include_directories(minift8_ui_smoke PRIVATE
    apps/minift8/include
    apps/minift8/src/ui_shell
)
target_compile_options(minift8_ui_smoke PRIVATE -Wall -Wextra -Wpedantic)

add_executable(linux_terminal_parser_unit
    tests/linux_terminal_parser_test.c
    platform/linux/linux_terminal_parser.c
)
target_include_directories(linux_terminal_parser_unit PRIVATE
    include
    platform/linux
)
target_compile_options(linux_terminal_parser_unit PRIVATE
    -Wall -Wextra -Werror -Wpedantic
)

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

add_test(
    NAME linux_portable_apps
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_portable_apps.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:hello>
)

add_test(
    NAME linux_nano
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_nano.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:hello>
)

add_test(
    NAME linux_directory
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_directory.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:hello>
            $<TARGET_FILE_DIR:dir_probe>
)

add_test(
    NAME linux_resources
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_resources.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:hello>
            $<TARGET_FILE_DIR:resource_probe>
)

add_test(
    NAME linux_audio
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_audio.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:audio_probe>
            ${CMAKE_SOURCE_DIR}/tests/kfs16b12k.wav
)

add_test(NAME minift8_ui_smoke COMMAND minift8_ui_smoke)
add_test(NAME linux_terminal_parser_unit COMMAND linux_terminal_parser_unit)

add_test(
    NAME linux_minift8
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_minift8.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:minift8>
)
