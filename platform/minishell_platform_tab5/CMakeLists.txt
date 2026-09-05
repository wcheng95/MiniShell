idf_component_register(
    SRCS
        "platform.c"
        "terminal_backend.c"
        "power_backend.c"
    INCLUDE_DIRS "."
    REQUIRES m5stack_tab5_noglib esp_driver_usb_serial_jtag esp_timer minishell_services minishell_transfer minishell_power
)
