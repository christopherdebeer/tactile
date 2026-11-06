idf_component_register(
  SRCS "main.c" "ble_mouse.c" "display.cpp" "sensors.c" "wifi_manager.c"
  INCLUDE_DIRS "."
  REQUIRES app_update bosch_bmi270 bosch_bmm150 bt captive_dns driver
           esp_event esp_hid esp_http_client esp_http_server esp_https_ota
           esp_netif esp_timer esp_wifi json mbedtls nvs_flash
)
