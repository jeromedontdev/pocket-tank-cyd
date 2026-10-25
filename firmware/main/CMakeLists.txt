idf_component_register(
    SRCS main.c director.c brightness.c ambient.c board_hardware_esp.c update_mode.c net_port_esp.c weather_port_esp.c
         display_port_cyd.c touch_port_xpt2046.c sd_port_cyd.c
    INCLUDE_DIRS "."
    REQUIRES tankcore esp_partition spi_flash esp_timer esp_driver_spi
             esp_driver_gpio esp_driver_ledc esp_adc esp_driver_uart esp_lcd
             nvs_flash esp_app_format esp_wifi esp_netif esp_event
             esp_http_client esp_https_ota app_update cjson mbedtls bootloader_support
             fatfs sdmmc esp_driver_sdspi esp_lcd_ili9341
)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}/cyd_assets")
target_compile_definitions(${COMPONENT_LIB} PRIVATE PT_BUILD_ID="${PT_BUILD_ID}")
add_dependencies(${COMPONENT_LIB} cyd_icon_assets)
set_source_files_properties(sd_port_cyd.c PROPERTIES
    OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/cyd_assets/icons_sd_bank.h")
set_property(SOURCE director.c update_mode.c net_port_esp.c weather_port_esp.c sd_port_cyd.c
    APPEND PROPERTY COMPILE_OPTIONS "-Os")
# The shared core references the weather backend in this archive.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u pt_board_marker" "-u weather_port_init")
