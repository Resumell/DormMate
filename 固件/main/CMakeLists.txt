idf_component_register(SRCS "main.c" "app_http.c" "app_control.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi nvs_flash esp_http_client cjson esp_driver_gpio led_strip esp_adc driver)