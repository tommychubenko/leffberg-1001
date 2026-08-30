set(TUYALINK_DIR ${COMPONENT_DIR}/tuya-connect-kit-for-mqtt-embedded-c)

set(include_dirs
    ${TUYALINK_DIR}/include
    ${TUYALINK_DIR}/interface
    ${TUYALINK_DIR}/utils
    ${TUYALINK_DIR}/middleware
)

set(srcs
    "port/system_wrapper.c"
    "port/ble_wrapper.c"
    "${TUYALINK_DIR}/src/tuya_ble_service.c"
    "${TUYALINK_DIR}/utils/log.c"
    "${TUYALINK_DIR}/utils/cJSON.c"
    "${TUYALINK_DIR}/utils/uni_md5.c"
    "${TUYALINK_DIR}/utils/aes_inf.c"
    "${TUYALINK_DIR}/utils/MultiTimer.c"
    "${TUYALINK_DIR}/utils/crc32.c"
)

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${include_dirs}"
                       REQUIRES nvs_flash bt esp_timer)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-pointer-sign
    -Wno-type-limits
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-format
    -Wno-error=maybe-uninitialized
)
