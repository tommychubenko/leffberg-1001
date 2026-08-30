#ifndef __TUYA_BLE_SERVICE_H__
#define __TUYA_BLE_SERVICE_H__

#include <stdint.h>
#include "tuya_cloud_types.h"
#include "tuya_wifi_provisioning.h"
#include "tuya_iot.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/
typedef struct {
    uint8_t *pid;
    uint8_t *uuid;
    uint8_t *auth_key;
}tuya_ble_service_init_params_t;

/* Connect-Kit stand-ins for TuyaOS combo ADV property / netcfg status APIs. */
typedef enum {
    TUYA_BLE_ADV_NETCFG_STATUS = 0,
} tuya_ble_adv_property_e;

typedef enum {
    TUYA_BLE_NETCFG_STATUS_UNCONFIG = 0, /* waiting for Wi-Fi credentials */
    TUYA_BLE_NETCFG_STATUS_CONFIGURED = 1,
} tuya_ble_netcfg_status_e;

typedef enum {
    TUYA_BLE_ADV_DATA_RECORD = 0,
} tuya_ble_adv_cb_type_e;

typedef struct {
    tuya_ble_adv_cb_type_e cb_type;
} tuya_ble_adv_param_t;

/***********************************************************
********************function declaration********************
***********************************************************/

typedef void (*ble_token_get_callback)(wifi_info_t wifi_info, tuya_binding_info_t binding_info);

int tuya_ble_service_start(tuya_ble_service_init_params_t *init_params, ble_token_get_callback cb);

/**
 * Combo netcfg init alias (TuyaOS: tuya_iot_bt_initializer).
 * Marks Connect-Kit BLE service as Wi-Fi+BLE combo provisioning ready.
 */
int tuya_iot_bt_initializer(void);

/** Set ADV property (Connect-Kit shim for tuya_ble_adv_property_set). */
int tuya_ble_adv_property_set(tuya_ble_adv_property_e prop, int value);

/**
 * MD5(UUID || AuthKey)[0..3] → out_hash[4].
 * If auth_key is NULL/empty, uses stored PID (when service started).
 */
int tuya_ble_calc_adv_hash(const char *uuid, const char *auth_key, uint8_t *out_hash);

/**
 * Rebuild ADV/ScanRSP without raw PID in 0xFD50 Service Data.
 * Service Data (~8 B): FC | Cap 0x00 0x0C | Flag 0x10 | AdvHash[4].
 */
int tuya_ble_adv_data_reconfig(void);

/** Ensure GATT server/services are registered before ADV (TuyaOS shim). */
int tuya_ble_gatt_send_data_init(void);

/** Push ADV payload and start ADV_IND (combo UNCONFIGURED). */
int tuya_ble_adv_start(void);

void tuya_ble_service_stop(void);

int ble_service_loop(void);

int ble_service_is_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_BLE_SERVICE_H__ */
