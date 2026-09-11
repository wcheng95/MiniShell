get_filename_component(MINISHELL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)

idf_component_register(
    SRCS "elfhello.c"
    INCLUDE_DIRS "${MINISHELL_ROOT}/include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wpedantic)
