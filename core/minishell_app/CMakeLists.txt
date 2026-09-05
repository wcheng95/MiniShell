idf_component_register(
    SRCS "app_manager.c"
    INCLUDE_DIRS "." "../../include"
    REQUIRES minishell_services elf_loader freertos
)
