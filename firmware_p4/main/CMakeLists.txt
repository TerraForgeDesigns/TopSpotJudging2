idf_component_register(
    SRCS
        main.c
        p4_hardware.c
        p4_battery.c
        battery_model.c
        p4_storage.c
        p4_network.c
        record_store.c
        car_status_store.c
        show_store.c
        p4_file_store.c
        p4_camera.c
        judging_model.c
        ui_theme.c
        ui_screens.c
    INCLUDE_DIRS .
    REQUIRES esp32_p4_wifi6_touch_lcd_7b lvgl__lvgl fatfs esp_video esp_driver_jpeg json esp_wifi esp_http_client esp_netif nvs_flash esp_adc)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
)
