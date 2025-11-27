idf_component_register(SRCS "esp32-wifi-keyboard.c"
                    INCLUDE_DIRS "."
                    REQUIRES espressif__esp_tinyusb esp_driver_uart esp_driver_gpio)
