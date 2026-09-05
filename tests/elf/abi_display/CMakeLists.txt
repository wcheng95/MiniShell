cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(abi_display)

include(elf_loader)
project_elf(abi_display)
