get_filename_component(MINISHELL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)

idf_component_register(
    SRCS
        "${MINISHELL_ROOT}/apps/keyer/main/keyer_main.c"
        "${MINISHELL_ROOT}/apps/keyer/src/app_controller/app_controller.c"
        "${MINISHELL_ROOT}/apps/keyer/src/config_service/config_service.c"
        "${MINISHELL_ROOT}/apps/keyer/src/keyer_engine/keyer_engine.c"
        "${MINISHELL_ROOT}/apps/keyer/src/keyer_engine/keyer_decoder.c"
        "${MINISHELL_ROOT}/apps/keyer/src/keyin/keyin.c"
        "${MINISHELL_ROOT}/apps/keyer/src/keyout/keyout.c"
        "${MINISHELL_ROOT}/apps/keyer/src/sidetone/sidetone.c"
    INCLUDE_DIRS
        "${MINISHELL_ROOT}/include"
        "${MINISHELL_ROOT}/apps/keyer/include"
        "${MINISHELL_ROOT}/apps/keyer/src/app_controller"
        "${MINISHELL_ROOT}/apps/keyer/src/config_service"
        "${MINISHELL_ROOT}/apps/keyer/src/keyer_engine"
        "${MINISHELL_ROOT}/apps/keyer/src/keyin"
        "${MINISHELL_ROOT}/apps/keyer/src/keyout"
        "${MINISHELL_ROOT}/apps/keyer/src/sidetone"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wpedantic)
