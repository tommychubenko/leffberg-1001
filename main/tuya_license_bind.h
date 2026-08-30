#ifndef TUYA_LICENSE_BIND_H_
#define TUYA_LICENSE_BIND_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tuya_iot.h"

typedef struct {
    char product_id[MAX_LENGTH_PRODUCT_ID + 1];
    char device_id[MAX_LENGTH_UUID + 1];
    char device_secret[MAX_LENGTH_AUTHKEY + 1];
    char hardware_name[64];
    char matched_mac[18];
} tuya_license_t;

/** Format 6-byte MAC as AA:BB:CC:DD:EE:FF (uppercase). */
void tuya_format_mac(const uint8_t mac[6], char out[18]);

/**
 * Read eFuse base MAC, log it, and resolve license from embedded registry.
 * Matches chip_mac against eFuse default MAC and Bluetooth MAC.
 */
bool tuya_license_resolve_for_chip(tuya_license_t *out);

#endif
