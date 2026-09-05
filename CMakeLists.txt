cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/core/minishell_services"
    "${CMAKE_SOURCE_DIR}/core/minishell_app"
    "${CMAKE_SOURCE_DIR}/core/minishell_shell"
    "${CMAKE_SOURCE_DIR}/platform/minishell_platform_tab5"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(minishell)
