cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(abi_input)

include(elf_loader)
project_elf(abi_input)
