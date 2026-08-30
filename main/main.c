#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

#include "MultiTimer.h"
#include "system_interface.h"
#include "tuya_ble_service.h"
#include "tuya_config.h"
#include "tuya_iot.h"
#include "tuya_license_bind.h"

static const char *TAG = "ai_speaker";

_Static_assert(sizeof(TUYA_PRODUCT_ID) - 1 == 16, "TUYA_PRODUCT_ID must be exactly 16 chars");
_Static_assert(sizeof(TUYA_DEVICE_UUID) - 1 == 16 || sizeof(TUYA_DEVICE_UUID) - 1 == 20,
               "TUYA_DEVICE_UUID must be exactly 16 or 20 chars");
_Static_assert(sizeof(TUYA_AUTH_KEY) - 1 == 32, "TUYA_AUTH_KEY must be exactly 32 chars");

static void tuya_token_cb(wifi_info_t wifi_info, tuya_binding_info_t binding_info)
{
    ESP_LOGI(TAG, "Tuya pairing token received, ssid=%s", wifi_info.ssid);
    (void)binding_info;
}

static void tuya_ble_task(void *arg)
{
    tuya_license_t license;
    if (!tuya_license_resolve_for_chip(&license)) {
        ESP_LOGE(TAG, "Tuya BLE not started: license/MAC mismatch");
        vTaskDelete(NULL);
        return;
    }

    /* Exactly 16-byte PID + NUL; auth_key zero-padded to 32 for connect-kit. */
    static uint8_t pid[MAX_LENGTH_PRODUCT_ID + 1];
    static uint8_t uuid[MAX_LENGTH_UUID + 1];
    static uint8_t auth_key[MAX_LENGTH_AUTHKEY + 1];

    memset(pid, 0, sizeof(pid));
    memset(uuid, 0, sizeof(uuid));
    memset(auth_key, 0, sizeof(auth_key));
    memcpy(pid, TUYA_PRODUCT_ID, 16);
    memcpy(uuid, TUYA_DEVICE_UUID, strlen(TUYA_DEVICE_UUID));
    memcpy(auth_key, TUYA_AUTH_KEY, strlen(TUYA_AUTH_KEY));

    tuya_ble_service_init_params_t params = {
        .pid = pid,
        .uuid = uuid,
        .auth_key = auth_key,
    };

    ESP_LOGI(TAG, "Start Connect-Kit combo ADV (AdvHash, no raw PID in 0xFD50)");
    ESP_LOGI(TAG, "PID[16]=%s UUID(%u)=%s auth_len=%u hw=%s",
             TUYA_PRODUCT_ID,
             (unsigned)strlen(TUYA_DEVICE_UUID), TUYA_DEVICE_UUID,
             (unsigned)strlen(TUYA_AUTH_KEY), license.hardware_name);

    vTaskDelay(pdMS_TO_TICKS(300));

    if (tuya_ble_service_start(&params, tuya_token_cb) != 0) {
        ESP_LOGE(TAG, "tuya_ble_service_start failed");
        vTaskDelete(NULL);
        return;
    }

    /* Combo provisioning + UNCONFIGURED; AdvHash via tuya_ble_calc_adv_hash. */
    (void)tuya_iot_bt_initializer();
    (void)tuya_ble_adv_property_set(TUYA_BLE_ADV_NETCFG_STATUS, TUYA_BLE_NETCFG_STATUS_UNCONFIG);
    {
        tuya_ble_adv_param_t adv_param = {0};
        uint8_t adv_hash[4] = {0};
        adv_param.cb_type = TUYA_BLE_ADV_DATA_RECORD;
        (void)adv_param;
        if (tuya_ble_calc_adv_hash((const char *)uuid, (const char *)auth_key, adv_hash) == 0) {
            ESP_LOGI(TAG, "AdvHash MD5(UUID||AuthKey)[0..3]=%02X%02X%02X%02X",
                     adv_hash[0], adv_hash[1], adv_hash[2], adv_hash[3]);
        }
    }
    /* BLE_SVC_STATUS_START → tuya_ble_adv_start() → tuya_ble_adv_data_reconfig(). */
    while (!ble_service_is_stop()) {
        ble_service_loop();
        system_sleep(10);
        MultiTimerYield();
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* Disable brownout detector during USB-powered BLE bring-up (prevents false reset). */
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    /* Keep PHY calibration / BT NVS across reboots (do not erase every boot). */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    xTaskCreate(tuya_ble_task, "tuya_ble", 8192, NULL, 5, NULL);
}
