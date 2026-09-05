idf_component_register(
    SRCS
        "nano.c"
        "nano_buffer.c"
        "nano_file.c"
        "nano_ui.c"
        "nano_util.c"
    INCLUDE_DIRS
        "."
        "../../../include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -ffreestanding -fno-builtin)
