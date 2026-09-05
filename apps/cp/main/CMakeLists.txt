idf_component_register(
    SRCS
        "cp.c"
        "cp_copy.c"
    INCLUDE_DIRS
        "."
        "../../../include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -ffreestanding -fno-builtin)
