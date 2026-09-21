include(${CMAKE_CURRENT_LIST_DIR}/../../../../../apps/minicw/sources.cmake)
idf_component_register(SRCS "${MINICW_ROOT}/apps/minicw/main/minicw_main.c" ${MINICW_SOURCES}
    INCLUDE_DIRS ${MINICW_INCLUDES})
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wpedantic -fno-builtin)
