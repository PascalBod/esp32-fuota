/**
 * This file is part of esp32-fuota.
 *
 * esp32-fuota is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * esp32-fuota is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with espidf-udp.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Copyright 2023 Pascal Bodin
 */

/**
 * Overview:
 *   This component scans the available Wi-Fi access points (APs), and
 *   returns their list. The component interface is blocking.
 *
 * Prerequisites:
 *   - The NVS must have been initialized (nvs_flash_init())
 *   - Wi-Fi must be inactive before the call to swb_scan_b()
 *
 * Usage:
 *   The client asks the component to scan APs with sw_scan_b().
 *   sw_scan_b() must be passed the maximum number of APs the client
 *   wants to get, and a pointer to the array where to store the APs.
 *
 *   At the end of the scan, sw_scan_b() returns the number of found
 *   APs. The AP array has been updated accordingly.
 *
 *   Does not return hidden APs.
 *
 *   This component is not reentrant: it must be used by one client
 *   task only, at any given time.
 */

#ifndef SCAN_WIFI_B_H_
#define SCAN_WIFI_B_H_

#include <stdint.h>

#include "esp_wifi.h"

// Task stack size
#define SW_STACK_DEPTH_MIN 2400

extern const char SWB_TAG[];

// Status values.
typedef enum {
    SWB_SUCCESS,
    SWB_ERROR,
} swb_status_t;

/**
 * Requests a scan of available APs.
 *
 * Returned APs are ordered by decreasing RSSI value.
 *
 * The ap_records array must not be modified by the client while a
 * scan request is being performed.
 *
 * Parameters:
 * - ap_nb: maximum number of APs to return
 * - ap_records: pointer to an array that can contain ap_nb APs
 * - found_ap_nb: pointer to the variable where sw_can_b writes the number
 *   of found APs
 *
 * Returned value:
 * - SWB_SUCCESS: successful scan
 * - SWB_ERROR: error in scan, returned values must be ignored
 */
swb_status_t swb_scan_b(uint8_t ap_nb, wifi_ap_record_t *ap_records,
                        uint8_t *found_ap_nb);

#endif /* SCAN_WIFI_B_H_ */
