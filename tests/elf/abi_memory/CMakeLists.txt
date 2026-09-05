cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(abi_memory)

include(elf_loader)
project_elf(abi_memory)
