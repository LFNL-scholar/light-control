if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/wifi_config.local.h")
    message(FATAL_ERROR "Copy wifi_config.example.h to wifi_config.local.h and configure local Wi-Fi credentials")
endif()
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/pack_assets.py"
    RESULT_VARIABLE pack_result)
if(NOT pack_result EQUAL 0)
    message(FATAL_ERROR "Failed to pack local web assets")
endif()
idf_component_register(SRCS "app_main.c" "cc1101.c" "waveform.c" "storage.c"
                  "lamp_controller.c" "web_server.c" "status_led.c" "history.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_driver_rmt esp_timer
                  esp_driver_usb_serial_jtag spiffs esp_rom esp_wifi esp_netif
                  esp_event nvs_flash esp_http_server
    EMBED_FILES "generated/index.html.gz" "generated/app.js.gz"
        "generated/scene.js.gz" "generated/stats.js.gz" "generated/three.module.min.js.gz"
        "apple-touch-icon.png")
