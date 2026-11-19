idf_component_register(SRCS "main.c" "esp_hid_gap.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_hid
                       PRIV_REQUIRES nvs_flash esp_timer esp_driver_usb_serial_jtag
                                     esp_vfs_console)
