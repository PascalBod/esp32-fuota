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
 *   This component starts and stops a Wi-Fi connection to a given access point
 *   (AP). The component interface is blocking.
 *
 * Prerequisites:
 *   - The NVS must be initialized (nvs_flash_init())
 *   - The TCP/IP stack must be initialized (esp_netif_init())
 *   - The default event loop must be started (esp_event_loop_create_default())
 *   - Wi-Fi must be inactive before the call to cwb_connect_b()
 *
 * Side effect:
 *   - An event handler is registered to the default loop
 *   - A task is started
 *
 * Usage:
 *   The client application calls cwb_connect_b() to connect to a given AP.
 *   When the connection is no more useful, it must be canceled by a call to
 *   cwb_disconnect_b().
 *
 *   If Wi-Fi connectivity is lost before next call to cwb_disconnect_b(),
 *   nothing special is done. The loss of connectivity will be discovered
 *   by the upper communication layer either when a reception timeout occurs,
 *   or when an error is returned by a transmission request. The upper layer
 *   must then call cwb_disconnect_b().
 *
 *   This component is not reentrant: it must be used by one client
 *   task only, at any given time.
 *
 *   CWB_SYS_ERR means that a serious system error occurred. Usually, the only
 *   way to react is to restart. It's up to the client application to do it.
 */

#ifndef CONN_WIFI_B_H_
#define CONN_WIFI_B_H_

#include <stdbool.h>
#include <stdint.h>

extern const char CWB_TAG[];

// Status values.
typedef enum {
    CWB_OK,
    CWB_CONN_ERR,
    CWB_IP_TIMEOUT,
    CWB_DIS,
    CWB_DIS_TIMEOUT,
    CWB_ALREADY_DIS,
    CWB_ALREADY_CON,
    CWB_PARAM_ERR,
    CWB_SYS_ERR,
} cwb_status_t;

/**
 * Tries to connect to the given AP, and waits for the assignment of an
 * IP address.
 *
 * Parameters:
 * - ssid: pointer to a null-terminated string of maximum 31 characters (not
 *   including the null character). Consequence: it seems that we can't connect
 *   to APs with an SSID of 32 bytes, or with an SSID containing null characters
 * - password: pointer to a null-terminated string of maximum 63 characters
 *   (not including the null character)
 * - ip_timeout_ms: time period after which the connection attempt is stopped
 *   if an IP address has not been assigned. In milliseconds
 *
 * Returned value:
 * - CWB_OK: connected to the Wi-FI AP, and got an IP address
 * - CWB_CONN_ERR: couldn't connect to the AP
 * - CWB_IP_TIMEOUT: couldn't get an IP address
 * - CWB_DIS: disconnected from the AP
 * - CWB_ALREADY_CON: already connected
 * - CWB_PARAM_ERR: pointer to SSID is null
 * - CWB_SYS_ERR: system error
 */
cwb_status_t cwb_connect_b(const uint8_t *ssid, const uint8_t *password,
                           uint32_t ip_timeout_ms);

/**
 * Disconnects from the current AP.
 *
 * Parameters: none
 *
 * Returned value:
 * - CWB_OK: disconnected
 * - CWB_ALREADY_DIS: already disconnected
 * - CWB_DIS_TIMEOUT: timeout on disconnection request
 * - CWB_SYS_ERR: system error
 */
cwb_status_t cwb_disconnect_b(void);

/**
 * Deinitializes Wi-Fi driver, if not done yet.
 *
 * Parameters: none
 *
 * Returned value:
 * - CWB_OK: Wi-Fi driver deinitialized
 * - CWB_SYS_ERR: system error
 */
cwb_status_t cwb_deinit_b(void);

#endif /* CONN_WIFI_B_H_ */
