idf_component_register(SRCS "provisioned-keyboard.c"
                    INCLUDE_DIRS "."
                    REQUIRES espressif__esp_tinyusb esp_driver_uart esp_driver_gpio
                            esp_netif esp_wifi nvs_flash espressif__cjson
                            espressif__qrcode espressif__network_provisioning
                            bt protocomm protobuf-c esp_timer openthread)
