cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(minicw)
# Xtensa ld aborts when stripping this image during link. Keep the same link
# and final elf-strip pass, but defer all stripping to that existing final pass.
# Patch a build-local module, never the managed dependency or resident build.
idf_component_get_property(MINICW_ELF_LOADER_DIR espressif__elf_loader COMPONENT_DIR)
file(READ "${MINICW_ELF_LOADER_DIR}/elf_loader.cmake" MINICW_ELF_LOADER)
set(MINICW_LINK_STRIP "    list(APPEND cflags -Wl,--strip-all\n                       -Wl,--strip-debug\n                       -Wl,--strip-discarded)")
string(FIND "${MINICW_ELF_LOADER}" "${MINICW_LINK_STRIP}" MINICW_STRIP_POSITION)
if(MINICW_STRIP_POSITION EQUAL -1)
    message(FATAL_ERROR "Mini-CW ELF packaging: upstream strip block changed")
endif()
string(REPLACE "${MINICW_LINK_STRIP}" "    # Stripping is performed after link." MINICW_ELF_LOADER "${MINICW_ELF_LOADER}")
file(WRITE "${CMAKE_BINARY_DIR}/minicw_elf_loader.cmake" "${MINICW_ELF_LOADER}")
include("${CMAKE_BINARY_DIR}/minicw_elf_loader.cmake")
execute_process(COMMAND ${CMAKE_C_COMPILER} -print-libgcc-file-name
    OUTPUT_VARIABLE MINICW_LIBGCC OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
set(ELF_LIBS "${MINICW_LIBGCC} -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/minicw.ld")
project_elf(minicw)
