idf_component_register(
    SRCS main.c director.c brightness.c update_mode.c net_port_esp.c
         display_port_cyd.c touch_port_xpt2046.c sd_port_cyd.c
    INCLUDE_DIRS "."
    REQUIRES tankcore esp_partition spi_flash esp_timer esp_driver_spi
             esp_driver_gpio esp_driver_ledc esp_driver_uart esp_lcd
             nvs_flash esp_app_format esp_wifi esp_netif esp_event
             esp_http_client esp_https_ota app_update cjson mbedtls bootloader_support
             fatfs sdmmc esp_driver_sdspi esp_lcd_ili9341
)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}/cyd_assets")
set_source_files_properties(sd_port_cyd.c PROPERTIES
    OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/cyd_assets/icons_sd_bank.h")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u pt_board_marker")
