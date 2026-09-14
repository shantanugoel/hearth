idf_component_register(
    SRCS "app_main.cc" "hearth_screens.cc" "hearth_wifi.cc"
         "hearth_config.cc" "hearth_audio.cc" "hearth_net.cc"
    INCLUDE_DIRS "."
    REQUIRES hearth_gfx zectrix_epd zectrix_board
             nvs_flash esp_wifi esp_netif esp_event
             esp_timer spi_flash
             esp_http_client console
             esp_driver_gpio heap
)
