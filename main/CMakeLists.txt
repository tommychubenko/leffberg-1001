get_filename_component(PROJECT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(TUYA_REGISTRY_JSON "${PROJECT_DIR}/tuya_devices_registry.json")
if(NOT EXISTS "${TUYA_REGISTRY_JSON}")
    message(FATAL_ERROR
        "Missing ${TUYA_REGISTRY_JSON}. Copy tuya_devices_registry.example.json "
        "to tuya_devices_registry.json and fill in device licenses.")
endif()

idf_component_register(SRCS "main.c" "tuya_license_bind.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash bt esp_event tuya-iot-link-sdk soc
                       EMBED_TXTFILES "${TUYA_REGISTRY_JSON}")
