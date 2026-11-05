idf_component_register(
  SRCS "main.c" "display.cpp"
  INCLUDE_DIRS "."
  REQUIRES app_update driver esp_event esp_http_client
           esp_https_ota esp_netif esp_timer esp_wifi json mbedtls nvs_flash
)
