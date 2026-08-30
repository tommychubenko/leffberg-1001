#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "ble_interface.h"
#include "tuya_ble_service.h"
#include "tuya_error_code.h"

static const char *TAG = "tuya_ble";
static const char *TUYA_BLE_TAG = "TUYA_BLE";

#define BLE_TX_PWR_LEVEL      ESP_PWR_LVL_N3  /* -3 dBm: reduce USB brownout on connect */

#define GATTS_APP_ID          0
#define GATT_READY_BIT        BIT0
#define ADV_DATA_READY_BIT    BIT1
#define SCAN_RSP_READY_BIT    BIT2
#define ADV_STARTED_BIT       BIT3
#define GATT_LOCAL_MTU        247
#define GATT_MTU_MIN          ESP_GATT_DEF_BLE_MTU_SIZE

static uint16_t s_negotiated_mtu = ESP_GATT_DEF_BLE_MTU_SIZE;

enum {
    IDX_SVC,
    IDX_CHAR_WRITE_DECL,
    IDX_CHAR_WRITE_VAL,
    IDX_CHAR_NOTIFY_DECL,
    IDX_CHAR_NOTIFY_VAL,
    IDX_CHAR_NOTIFY_CCC,
    IDX_NB,
};

static TKL_BLE_GAP_EVT_FUNC_CB s_gap_cb;
static TKL_BLE_GATT_EVT_FUNC_CB s_gatt_cb;
static EventGroupHandle_t s_ble_events;
static SemaphoreHandle_t s_adv_lock;
static bool s_stack_ready;
static bool s_adv_pending;
static bool s_adv_running;
static TKL_BLE_GAP_ADV_PARAMS_T s_pending_adv;
static uint16_t s_gatts_if;
static esp_ble_addr_type_t s_own_addr_type = BLE_ADDR_TYPE_PUBLIC;
static uint16_t s_conn_handle = TKL_BLE_GATT_INVALID_HANDLE;
static uint16_t s_write_handle;
static uint16_t s_notify_handle;
static uint16_t s_ccc_handle;

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t char_prop_write = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t char_prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint16_t tuya_svc_uuid = 0xFD50;
/* Tuya write/notify: 00000001/00000002-0000-1001-8001-00805F9B07D0 */
static const uint8_t tuya_write_uuid128[16] = {
    0xD0, 0x07, 0x9B, 0x5F, 0x80, 0x00, 0x01, 0x80,
    0x01, 0x10, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00
};
static const uint8_t tuya_notify_uuid128[16] = {
    0xD0, 0x07, 0x9B, 0x5F, 0x80, 0x00, 0x01, 0x80,
    0x01, 0x10, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00
};
static uint8_t s_char_value[244];
static uint8_t s_ccc[2];

static const esp_gatts_attr_db_t s_gatt_db[IDX_NB] = {
    [IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
         sizeof(uint16_t), sizeof(tuya_svc_uuid), (uint8_t *)&tuya_svc_uuid}
    },
    [IDX_CHAR_WRITE_DECL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&char_prop_write}
    },
    /* AUTO_RSP: nRF Connect service discovery must not hang on unread READ. */
    [IDX_CHAR_WRITE_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)tuya_write_uuid128,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(s_char_value), sizeof(s_char_value), s_char_value}
    },
    [IDX_CHAR_NOTIFY_DECL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&char_prop_notify}
    },
    [IDX_CHAR_NOTIFY_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)tuya_notify_uuid128, ESP_GATT_PERM_READ,
         sizeof(s_char_value), 0, s_char_value}
    },
    [IDX_CHAR_NOTIFY_CCC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(s_ccc), sizeof(s_ccc), s_ccc}
    },
};

static uint16_t clamp_adv_interval(uint16_t units)
{
    if (units < 0x0020) {
        return 0x0020;
    }
    if (units > 0x4000) {
        return 0x4000;
    }
    return units;
}

static void apply_conn_params(const esp_bd_addr_t bda)
{
    esp_ble_gap_set_prefer_conn_params((uint8_t *)bda, 0x0010, 0x0020, 0, 400);

    esp_ble_conn_update_params_t conn_params = {0};
    memcpy(conn_params.bda, bda, sizeof(esp_bd_addr_t));
    conn_params.latency = 0;
    conn_params.max_int = 0x20; /* 40 ms */
    conn_params.min_int = 0x10; /* 20 ms */
    conn_params.timeout = 400;  /* 4000 ms */
    esp_err_t err = esp_ble_gap_update_conn_params(&conn_params);
    if (err != ESP_OK) {
        ESP_LOGW("TUYA_BLE", "update_conn_params: %s", esp_err_to_name(err));
    }
}

static EventBits_t wait_ble_event_bits(EventBits_t bits_to_wait, bool wait_all, uint32_t timeout_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline) {
        EventBits_t bits = xEventGroupGetBits(s_ble_events);
        if (wait_all) {
            if ((bits & bits_to_wait) == bits_to_wait) {
                return bits;
            }
        } else if (bits & bits_to_wait) {
            return bits;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return xEventGroupGetBits(s_ble_events);
}


static void start_advertising_now(void)
{
    if (xSemaphoreTake(s_adv_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return;
    }

    if (s_conn_handle != TKL_BLE_GATT_INVALID_HANDLE) {
        ESP_LOGW(TAG, "skip ADV start: still connected conn_id=%u", s_conn_handle);
        xSemaphoreGive(s_adv_lock);
        return;
    }

    if (s_adv_running) {
        esp_ble_gap_stop_advertising();
        vTaskDelay(pdMS_TO_TICKS(50));
        s_adv_running = false;
    }

    esp_ble_adv_params_t adv = {0};
    adv.adv_int_min = clamp_adv_interval(s_pending_adv.adv_interval_min);
    adv.adv_int_max = clamp_adv_interval(s_pending_adv.adv_interval_max);
    if (adv.adv_int_min < 0x00A0) {
        adv.adv_int_min = 0x00A0;
    }
    if (adv.adv_int_max < 0x00F0) {
        adv.adv_int_max = 0x00F0;
    }
    if (adv.adv_int_max < adv.adv_int_min) {
        adv.adv_int_max = adv.adv_int_min;
    }
    adv.adv_type = ADV_TYPE_IND;
    adv.own_addr_type = s_own_addr_type;
    adv.channel_map = ADV_CHNL_ALL;
    adv.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    xEventGroupClearBits(s_ble_events, ADV_STARTED_BIT);
    esp_err_t err = esp_ble_gap_start_advertising(&adv);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start advertising failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "request ADV_IND (UUID 0xFD50, MID 0x07D0, connectable)");
    }
    xSemaphoreGive(s_adv_lock);
}

static void tuya_ble_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (param == NULL) {
        ESP_LOGE(TUYA_BLE_TAG, "GAP event=%d ignored: NULL param", (int)event);
        return;
    }

    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        if (param->adv_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TUYA_BLE_TAG, "ADV data set failed status=%d", param->adv_data_raw_cmpl.status);
        }
        xEventGroupSetBits(s_ble_events, ADV_DATA_READY_BIT);
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        if (param->scan_rsp_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TUYA_BLE_TAG, "Scan RSP set failed status=%d", param->scan_rsp_data_raw_cmpl.status);
        }
        xEventGroupSetBits(s_ble_events, SCAN_RSP_READY_BIT);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            s_adv_running = false;
            ESP_LOGE(TUYA_BLE_TAG, "ADV start failed status=%d", param->adv_start_cmpl.status);
        } else {
            s_adv_running = true;
            xEventGroupSetBits(s_ble_events, ADV_STARTED_BIT);
            ESP_LOGI("TUYA_BLE", "ADV started successfully");
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        s_adv_running = false;
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGW(TUYA_BLE_TAG, "ADV stop status=%d", param->adv_stop_cmpl.status);
        } else {
            ESP_LOGI(TAG, "ADV stopped");
        }
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI("TUYA_BLE", "Connection params updated: status=%d",
                 param->update_conn_params.status);
        if (param->update_conn_params.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TUYA_BLE_TAG, "Connection params rejected int=%u lat=%u timeout=%u",
                     param->update_conn_params.conn_int,
                     param->update_conn_params.latency,
                     param->update_conn_params.timeout);
        }
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (param->ble_security.auth_cmpl.success) {
            ESP_LOGI(TAG, "pairing complete addr=%02X:%02X:%02X:%02X:%02X:%02X",
                     param->ble_security.auth_cmpl.bd_addr[0],
                     param->ble_security.auth_cmpl.bd_addr[1],
                     param->ble_security.auth_cmpl.bd_addr[2],
                     param->ble_security.auth_cmpl.bd_addr[3],
                     param->ble_security.auth_cmpl.bd_addr[4],
                     param->ble_security.auth_cmpl.bd_addr[5]);
        } else {
            ESP_LOGE(TUYA_BLE_TAG, "pairing failed reason=0x%02x",
                     param->ble_security.auth_cmpl.fail_reason);
        }
        break;
    case ESP_GAP_BLE_PHY_UPDATE_COMPLETE_EVT:
        if (param->phy_update.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TUYA_BLE_TAG, "PHY update failed status=%d", param->phy_update.status);
        }
        break;
    default:
        ESP_LOGD(TUYA_BLE_TAG, "GAP event=%d (unhandled)", (int)event);
        break;
    }
}

static void tuya_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                     esp_ble_gatts_cb_param_t *param)
{
    if (param == NULL) {
        ESP_LOGE(TUYA_BLE_TAG, "GATTS event=%d ignored: NULL param", (int)event);
        return;
    }
    if (event != ESP_GATTS_REG_EVT && s_gatts_if != ESP_GATT_IF_NONE && gatts_if != s_gatts_if) {
        return;
    }

    switch (event) {
    case ESP_GATTS_REG_EVT:
        if (param->reg.status != ESP_GATT_OK) {
            ESP_LOGE(TUYA_BLE_TAG, "GATTS app register failed status=%d", param->reg.status);
            return;
        }
        s_gatts_if = gatts_if;
        ESP_LOGI(TAG, "GATTS REG app_id=%u gatts_if=%u", param->reg.app_id, gatts_if);
        {
            esp_bd_addr_t local_bda = {0};
            uint8_t local_type = BLE_ADDR_TYPE_PUBLIC;
            if (esp_ble_gap_get_local_used_addr(local_bda, &local_type) == ESP_OK) {
                s_own_addr_type = (esp_ble_addr_type_t)local_type;
                ESP_LOGI(TAG, "local BLE addr %02X:%02X:%02X:%02X:%02X:%02X type=%s",
                         local_bda[0], local_bda[1], local_bda[2],
                         local_bda[3], local_bda[4], local_bda[5],
                         local_type == BLE_ADDR_TYPE_RANDOM ? "RANDOM" : "PUBLIC");
            } else {
                s_own_addr_type = BLE_ADDR_TYPE_PUBLIC;
            }
        }
        esp_ble_gap_set_device_name("TY");
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, BLE_TX_PWR_LEVEL);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, BLE_TX_PWR_LEVEL);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL0, BLE_TX_PWR_LEVEL);
        ESP_LOGI(TUYA_BLE_TAG, "BLE TX power set to -3 dBm (brownout-safe)");
        /* Open pairing for nRF Connect / Smart Life discovery — no MITM. */
        {
            esp_ble_auth_req_t auth_req = ESP_LE_AUTH_NO_BOND;
            esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
            uint8_t key_size = 16;
            uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
            uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
            esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
            esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
            esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
            esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key));
            esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(rsp_key));
        }
        esp_ble_gatts_create_attr_tab(s_gatt_db, gatts_if, IDX_NB, 0);
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status != ESP_GATT_OK || param->add_attr_tab.num_handle != IDX_NB) {
            ESP_LOGE(TUYA_BLE_TAG, "create attr tab failed status=%d handles=%u",
                     param->add_attr_tab.status, param->add_attr_tab.num_handle);
            return;
        }
        s_write_handle = param->add_attr_tab.handles[IDX_CHAR_WRITE_VAL];
        s_notify_handle = param->add_attr_tab.handles[IDX_CHAR_NOTIFY_VAL];
        s_ccc_handle = param->add_attr_tab.handles[IDX_CHAR_NOTIFY_CCC];
        esp_ble_gatts_start_service(param->add_attr_tab.handles[IDX_SVC]);
        xEventGroupSetBits(s_ble_events, GATT_READY_BIT);
        ESP_LOGI(TAG, "GATT 0xFD50 ready write=0x%04x notify=0x%04x ccc=0x%04x",
                 s_write_handle, s_notify_handle, s_ccc_handle);
        break;

    case ESP_GATTS_START_EVT:
        if (param->start.status != ESP_GATT_OK) {
            ESP_LOGE(TUYA_BLE_TAG, "GATT service start failed status=%d handle=0x%04x",
                     param->start.status, param->start.service_handle);
        } else {
            ESP_LOGI(TAG, "GATT service started handle=0x%04x", param->start.service_handle);
        }
        break;

    case ESP_GATTS_CONNECT_EVT: {
        s_conn_handle = param->connect.conn_id;
        s_adv_running = false;
        s_negotiated_mtu = ESP_GATT_DEF_BLE_MTU_SIZE;

        ESP_LOGI("TUYA_BLE", ">>> CONNECTED: conn_id=%d, remote BDA: %02x:%02x:%02x:%02x:%02x:%02x",
                 param->connect.conn_id,
                 param->connect.remote_bda[0], param->connect.remote_bda[1],
                 param->connect.remote_bda[2], param->connect.remote_bda[3],
                 param->connect.remote_bda[4], param->connect.remote_bda[5]);
        esp_ble_gap_stop_advertising();
        (void)esp_ble_gatt_set_local_mtu(GATT_LOCAL_MTU);
        apply_conn_params(param->connect.remote_bda);

        TKL_BLE_GAP_PARAMS_EVT_T gap_event = {0};
        gap_event.type = TKL_BLE_GAP_EVT_CONNECT;
        gap_event.conn_handle = param->connect.conn_id;
        gap_event.result = 0;
        gap_event.gap_event.connect.role = TKL_BLE_ROLE_SERVER;
        if (s_gap_cb) {
            s_gap_cb(&gap_event);
        }
        break;
    }

    case ESP_GATTS_DISCONNECT_EVT: {
        ESP_LOGI("TUYA_BLE", "GATT Client disconnected, restarting ADV...");

        TKL_BLE_GAP_PARAMS_EVT_T gap_event = {0};
        gap_event.type = TKL_BLE_GAP_EVT_DISCONNECT;
        gap_event.conn_handle = param->disconnect.conn_id;
        gap_event.gap_event.disconnect.reason = param->disconnect.reason;
        gap_event.gap_event.disconnect.role = TKL_BLE_ROLE_SERVER;
        s_conn_handle = TKL_BLE_GATT_INVALID_HANDLE;
        s_negotiated_mtu = ESP_GATT_DEF_BLE_MTU_SIZE;
        if (s_gap_cb) {
            s_gap_cb(&gap_event);
        }
        /* The service callback queues BLE_SVC_STATUS_DISCONNECT and owns ADV restart. */
        break;
    }

    case ESP_GATTS_MTU_EVT: {
        uint16_t client_mtu = param->mtu.mtu;
        uint16_t confirmed_mtu = client_mtu;

        if (confirmed_mtu > GATT_LOCAL_MTU) {
            confirmed_mtu = GATT_LOCAL_MTU;
        }
        if (confirmed_mtu < GATT_MTU_MIN) {
            confirmed_mtu = GATT_MTU_MIN;
        }
        s_negotiated_mtu = confirmed_mtu;

        /* Bluedroid already sent ATT MTU Response; log negotiated size for the client. */
        ESP_LOGI("TUYA_BLE", "MTU exchange confirmed conn_id=%u client=%u server=%u",
                 param->mtu.conn_id, client_mtu, confirmed_mtu);
        break;
    }

    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle == s_ccc_handle) {
            ESP_LOGI(TAG, "CCC write len=%u val=%02x%02x",
                     param->write.len,
                     param->write.len > 0 ? param->write.value[0] : 0,
                     param->write.len > 1 ? param->write.value[1] : 0);
        } else if (param->write.handle == s_write_handle) {
            if (param->write.len > 0 && param->write.value == NULL) {
                ESP_LOGE(TUYA_BLE_TAG, "GATT write NULL value len=%u", param->write.len);
                break;
            }
            TKL_BLE_GATT_PARAMS_EVT_T gatt_event = {0};
            gatt_event.type = TKL_BLE_GATT_EVT_WRITE_REQ;
            gatt_event.conn_handle = param->write.conn_id;
            gatt_event.gatt_event.write_report.char_handle = param->write.handle;
            gatt_event.gatt_event.write_report.report.length = param->write.len;
            gatt_event.gatt_event.write_report.report.p_data = param->write.value;
            if (s_gatt_cb) {
                s_gatt_cb(&gatt_event);
            }
        }
        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id,
                                       ESP_GATT_OK, NULL);
        }
        break;

    case ESP_GATTS_RESPONSE_EVT:
        if (param->rsp.status != ESP_GATT_OK) {
            ESP_LOGE(TUYA_BLE_TAG, "GATTS response error status=%d handle=0x%04x",
                     param->rsp.status, param->rsp.handle);
        }
        break;

    case ESP_GATTS_READ_EVT:
        /* AUTO_RSP attributes are answered by stack; keep log for debugging. */
        ESP_LOGD(TAG, "GATT READ handle=0x%04x", param->read.handle);
        break;

    default:
        ESP_LOGD(TUYA_BLE_TAG, "GATTS event=%d (unhandled)", (int)event);
        break;
    }
}

static OPERATE_RET ble_stack_ensure(void)
{
    if (s_stack_ready) {
        return OPRT_OK;
    }

    s_ble_events = xEventGroupCreate();
    s_adv_lock = xSemaphoreCreateMutex();
    if (!s_ble_events || !s_adv_lock) {
        return OPRT_MALLOC_FAILED;
    }

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (esp_bt_controller_init(&bt_cfg) != ESP_OK) {
        return OPRT_COM_ERROR;
    }
    if (esp_bt_controller_enable(ESP_BT_MODE_BLE) != ESP_OK) {
        return OPRT_COM_ERROR;
    }
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    if (esp_bluedroid_init_with_cfg(&bluedroid_cfg) != ESP_OK) {
        return OPRT_COM_ERROR;
    }
    if (esp_bluedroid_enable() != ESP_OK) {
        return OPRT_COM_ERROR;
    }

    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(tuya_gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(tuya_ble_gap_event_handler));
    ESP_LOGI(TUYA_BLE_TAG, "GAP/GATTS callbacks registered (tuya_ble_gap_event_handler)");
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(GATTS_APP_ID));
    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(GATT_LOCAL_MTU));

    EventBits_t bits = wait_ble_event_bits(GATT_READY_BIT, true, 5000);
    if ((bits & GATT_READY_BIT) == 0) {
        ESP_LOGE(TAG, "GATT table timeout");
        return OPRT_COM_ERROR;
    }

    s_stack_ready = true;
    ESP_LOGI(TAG, "Bluedroid GATT server ready (app_id=%u)", GATTS_APP_ID);
    return OPRT_OK;
}

OPERATE_RET tkl_ble_stack_init(uint8_t role)
{
    (void)role;
    return ble_stack_ensure();
}

OPERATE_RET tkl_ble_stack_deinit(uint8_t role)
{
    (void)role;
    return OPRT_OK;
}

OPERATE_RET tkl_ble_gap_callback_register(const TKL_BLE_GAP_EVT_FUNC_CB gap_evt)
{
    s_gap_cb = gap_evt;
    return OPRT_OK;
}

OPERATE_RET tkl_ble_gatt_callback_register(const TKL_BLE_GATT_EVT_FUNC_CB gatt_evt)
{
    s_gatt_cb = gatt_evt;
    return OPRT_OK;
}

OPERATE_RET tkl_ble_gap_adv_rsp_data_set(TKL_BLE_DATA_T const *p_adv, TKL_BLE_DATA_T const *p_scan_rsp)
{
    OPERATE_RET rt = ble_stack_ensure();
    if (rt != OPRT_OK) {
        return rt;
    }
    xEventGroupClearBits(s_ble_events, ADV_DATA_READY_BIT | SCAN_RSP_READY_BIT | ADV_STARTED_BIT);
    if (p_adv && p_adv->p_data && p_adv->length) {
        ESP_LOGI(TAG, "ADV raw %u bytes (0xFD50 Service Data ~8B + AdvHash)", (unsigned)p_adv->length);
        ESP_LOG_BUFFER_HEX(TAG, p_adv->p_data, p_adv->length);
        if (esp_ble_gap_config_adv_data_raw(p_adv->p_data, p_adv->length) != ESP_OK) {
            return OPRT_COM_ERROR;
        }
    }
    if (p_scan_rsp && p_scan_rsp->p_data && p_scan_rsp->length) {
        ESP_LOGI(TAG, "Scan RSP raw %u bytes (expect MID 0x07D0)", (unsigned)p_scan_rsp->length);
        ESP_LOG_BUFFER_HEX(TAG, p_scan_rsp->p_data, p_scan_rsp->length);
        if (esp_ble_gap_config_scan_rsp_data_raw(p_scan_rsp->p_data, p_scan_rsp->length) != ESP_OK) {
            return OPRT_COM_ERROR;
        }
    }
    return OPRT_OK;
}

OPERATE_RET tkl_ble_gap_adv_start(TKL_BLE_GAP_ADV_PARAMS_T const *p_adv_params)
{
    if (!p_adv_params) {
        return OPRT_INVALID_PARM;
    }
    s_pending_adv = *p_adv_params;
    s_adv_pending = true;

    EventBits_t bits = wait_ble_event_bits(ADV_DATA_READY_BIT | SCAN_RSP_READY_BIT, true, 2000);
    if ((bits & ADV_DATA_READY_BIT) == 0 || (bits & SCAN_RSP_READY_BIT) == 0) {
        ESP_LOGW(TAG, "advertising start deferred until ADV/RSP ready");
        return OPRT_OK;
    }

    s_adv_pending = false;
    start_advertising_now();
    return OPRT_OK;
}

OPERATE_RET tkl_ble_gap_adv_stop(void)
{
    s_adv_pending = false;
    esp_ble_gap_stop_advertising();
    s_adv_running = false;
    return OPRT_OK;
}

OPERATE_RET tkl_ble_gap_disconnect(uint16_t conn_handle, uint8_t hci_reason)
{
    (void)hci_reason;
    esp_ble_gatts_close(s_gatts_if, conn_handle);
    return OPRT_OK;
}

OPERATE_RET tkl_ble_gatts_service_add(TKL_BLE_GATTS_PARAMS_T *p_service)
{
    OPERATE_RET rt = ble_stack_ensure();
    if (rt != OPRT_OK) {
        return rt;
    }
    if (!p_service || !p_service->p_service || !p_service->p_service->p_char) {
        return OPRT_INVALID_PARM;
    }
    p_service->p_service->p_char[0].handle = s_write_handle;
    p_service->p_service->p_char[1].handle = s_notify_handle;
    return OPRT_OK;
}

OPERATE_RET tkl_ble_gatts_value_notify(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length)
{
    if (s_gatts_if == 0 || p_data == NULL) {
        return OPRT_COM_ERROR;
    }
    esp_err_t err = esp_ble_gatts_send_indicate(s_gatts_if, conn_handle, char_handle, length, p_data, false);
    return (err == ESP_OK) ? OPRT_OK : OPRT_COM_ERROR;
}
