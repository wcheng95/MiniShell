cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(minicw)
include(elf_loader)
execute_process(COMMAND ${CMAKE_C_COMPILER} -print-libgcc-file-name
    OUTPUT_VARIABLE MINICW_LIBGCC OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
set(ELF_LIBS "${MINICW_LIBGCC} -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/minicw.ld")
project_elf(minicw)
