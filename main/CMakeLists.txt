idf_component_register(
    SRCS "main.c" "usb_descriptors.c" "macro_engine.c" "display.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer espressif__tinyusb usb espressif__esp_lcd_co5300 esp_lcd esp_driver_spi esp_driver_gpio
)

idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
