cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(keyer)

include(elf_loader)
project_elf(keyer)
