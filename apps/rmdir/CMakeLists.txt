cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rmdir)

include(elf_loader)
project_elf(rmdir)
