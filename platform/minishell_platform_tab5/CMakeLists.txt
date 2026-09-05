idf_component_register(
    SRCS "platform.c"
    INCLUDE_DIRS "."
    REQUIRES m5stack_tab5_noglib esp_driver_usb_serial_jtag
)
