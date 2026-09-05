idf_component_register(
    SRCS
        "services.c"
        "system_service.c"
        "memory_service.c"
        "filesystem_service.c"
        "time_location_service.c"
        "display_service.c"
        "input_service.c"
    INCLUDE_DIRS "." "../../include"
)
