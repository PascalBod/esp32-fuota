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
 * along with esp32-fuota. If not, see <https://www.gnu.org/licenses/>.
 *
 * Copyright 2023 Pascal Bodin
 */

/**
 * Overview:
 *   This component performs an Over The Air (OTA) update, using an existing
 *   network connection.
 *
 * Prerequisites:
 *   - A partition table with the three partitions used for OTA: factory,
 *     OTA_0 and OTA_1
 *   - The default event loop must be started (esp_event_loop_create_default())
 *   - The NVS must be initialized (nvs_flash_init())
 *   - The TCP/IP stack must be initialized (esp_netif_init())
 *   - Network connectivity must be available when the task is started
 *
 * Usage:
 *   The client requests an update by calling ota_update_b(). The function
 *   returns the result: failure (for instance if connectivity is lost),
 *   system error, no update available, update received and installed.
 */

#ifndef FUOTA_B_H_
#define FUOTA_B_H_

#include <stdint.h>

extern const char OTA_TAG[];

//Status values.
typedef enum {
    OTA_OK,
    OTA_UPDATED,
    OTA_PARAM_ERR,
    OTA_NO_UPDATE,
    OTA_CONN_ERR,
    OTA_SYS_ERR,
} ota_status_t;

/**
 * Requests an OTA firmware update.
 *
 * Input parameters:
 * - server name: pointer to a 0-terminated string containing the FQDN
 *   of the update server
 * - server port: update server port
 * - cert_pem: pointer to a byte array containing the certificate in
 *   PEM format used for connecting to the server over TLS.
 * - username: pointer to a 0-terminated string containing the username
 *   used for basic authentication on the update server
 * - password: pointer to a 0-terminated string containing the password
 *   used for basic authentication on the update server
 * - id: pointer to a 0-terminated string containing the identifier of
 *   the device
 * - app_ver: pointer to a 0-terminated string containing the version of
 *   the application
 *
 * Returned value:
 * - OTA_UPDATED: update received and stored
 * - OTA_NO_UPDATE: no update available
 * - OTA_PARAM_ERR: incorrect OTA parameter
 * - OTA_SYS_ERR: system error, a restart could be good
 * - OTA_CONN_ERR: chances are high that there was a connectivity probleme
 */
ota_status_t ota_update_b(const char *server_name, uint16_t server_port,
                          const char *cert_pem, const  char *username,
                          const char *password,
                          const char *id,
                          const char *app_ver);

#endif /* FUOTA_B_H_ */
