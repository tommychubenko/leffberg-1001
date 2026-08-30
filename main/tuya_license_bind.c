#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "tuya_license_bind.h"

static const char *TAG = "tuya_license";

extern const uint8_t tuya_devices_registry_json_start[] asm("_binary_tuya_devices_registry_json_start");
extern const uint8_t tuya_devices_registry_json_end[] asm("_binary_tuya_devices_registry_json_end");

void tuya_format_mac(const uint8_t mac[6], char out[18])
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void mac_to_upper(char *s)
{
    for (; *s; ++s) {
        *s = (char)toupper((unsigned char)*s);
    }
}

static bool mac_equal(const char *a, const char *b)
{
    char aa[18];
    char bb[18];
    snprintf(aa, sizeof(aa), "%s", a);
    snprintf(bb, sizeof(bb), "%s", b);
    mac_to_upper(aa);
    mac_to_upper(bb);
    return strcmp(aa, bb) == 0;
}

static bool fill_from_device(const cJSON *dev, const char *product_id,
                             const char *matched_mac, tuya_license_t *out)
{
    const cJSON *device_id = cJSON_GetObjectItemCaseSensitive(dev, "device_id");
    const cJSON *device_secret = cJSON_GetObjectItemCaseSensitive(dev, "device_secret");
    const cJSON *hardware_name = cJSON_GetObjectItemCaseSensitive(dev, "hardware_name");

    if (!cJSON_IsString(device_id) || !cJSON_IsString(device_secret)) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->product_id, sizeof(out->product_id), "%s", product_id);
    snprintf(out->device_id, sizeof(out->device_id), "%s", device_id->valuestring);
    snprintf(out->device_secret, sizeof(out->device_secret), "%s", device_secret->valuestring);
    snprintf(out->matched_mac, sizeof(out->matched_mac), "%s", matched_mac);
    if (cJSON_IsString(hardware_name)) {
        snprintf(out->hardware_name, sizeof(out->hardware_name), "%s", hardware_name->valuestring);
    }
    return true;
}

bool tuya_license_resolve_for_chip(tuya_license_t *out)
{
    if (!out) {
        return false;
    }

    uint8_t efuse_mac[6] = {0};
    uint8_t bt_mac[6] = {0};
    char efuse_str[18] = {0};
    char bt_str[18] = {0};

    if (esp_efuse_mac_get_default(efuse_mac) != ESP_OK) {
        ESP_LOGE(TAG, "esp_efuse_mac_get_default failed");
        return false;
    }
    tuya_format_mac(efuse_mac, efuse_str);
    ESP_LOGI(TAG, "Chip eFuse MAC: %s", efuse_str);

    if (esp_read_mac(bt_mac, ESP_MAC_BT) == ESP_OK) {
        tuya_format_mac(bt_mac, bt_str);
        ESP_LOGI(TAG, "Bluetooth MAC: %s", bt_str);
    }

    size_t json_len = (size_t)(tuya_devices_registry_json_end - tuya_devices_registry_json_start);
    char *json = calloc(1, json_len + 1);
    if (!json) {
        ESP_LOGE(TAG, "registry alloc failed");
        return false;
    }
    memcpy(json, tuya_devices_registry_json_start, json_len);

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        ESP_LOGE(TAG, "failed to parse tuya_devices_registry.json");
        return false;
    }

    const cJSON *product_id = cJSON_GetObjectItemCaseSensitive(root, "product_id");
    const cJSON *devices = cJSON_GetObjectItemCaseSensitive(root, "devices");
    if (!cJSON_IsString(product_id) || !cJSON_IsArray(devices)) {
        ESP_LOGE(TAG, "invalid registry schema");
        cJSON_Delete(root);
        return false;
    }

    bool found = false;
    const cJSON *dev = NULL;
    cJSON_ArrayForEach(dev, devices) {
        const cJSON *chip_mac = cJSON_GetObjectItemCaseSensitive(dev, "chip_mac");
        if (!cJSON_IsString(chip_mac)) {
            continue;
        }
        if (mac_equal(chip_mac->valuestring, efuse_str)) {
            found = fill_from_device(dev, product_id->valuestring, efuse_str, out);
            break;
        }
        if (bt_str[0] && mac_equal(chip_mac->valuestring, bt_str)) {
            found = fill_from_device(dev, product_id->valuestring, bt_str, out);
            break;
        }
    }

    cJSON_Delete(root);

    if (!found) {
        ESP_LOGE(TAG, "No Tuya license bound to MAC %s (BT %s)", efuse_str,
                 bt_str[0] ? bt_str : "n/a");
        return false;
    }

    ESP_LOGI(TAG, "License matched: %s mac=%s uuid=%s",
             out->hardware_name[0] ? out->hardware_name : "(unnamed)",
             out->matched_mac, out->device_id);
    return true;
}
