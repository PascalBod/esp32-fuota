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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include "conn_wifi_b.h"
#include "fuota_b.h"
#include "scan_wifi_b.h"

// Automaton states.
typedef enum {
    ST_SCAN,
    ST_TRY_OTA,
} state_t;

//-------------------------------------------------------------------
// OTA update configuration values.

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");

static const char OTA_VERSION[] = "0.1.0";
static const char OTA_UPDATE_AP_SSID[] = CONFIG_FUO_OTA_UPDATE_AP_SSID;
static const char OTA_UPDATE_AP_PASSWORD[] = CONFIG_FUO_OTA_UPDATE_AP_PASSWORD;
static const char OTA_SERVER_NAME[] = CONFIG_FUO_OTA_SERVER_NAME;
static uint16_t OTA_SERVER_PORT = CONFIG_FUO_OTA_SERVER_PORT;
static const char OTA_SERVER_USERNAME[] = CONFIG_FUO_OTA_SERVER_USERNAME;
static const char OTA_SERVER_PASSWORD[] = CONFIG_FUO_OTA_SERVER_PASSWORD;

//-------------------------------------------------------------------
// Misc. configuration values.

// Time period before starting operations, right after application
// startup, in ms.
static const uint32_t WAIT_BEFORE_START_PERIOD_MS = 30000;

// Time period before restart in case of fatal error, in ms.
static const uint32_t WAIT_BEFORE_RESTART_PERIOD_MS = 30000;

// Time period before performing next scan, in ms.
static const uint32_t WAIT_BEFORE_NEXT_SCAN_MS = 30000;

// Maximum wait period to get an IP address.
static const uint32_t IP_TIMEOUT_MS = 5000;

// Identifier used for identification on OTA server.
const char DEV_ID[] = "00001";

static const char APP_TAG[] = "APP";

// Max number of APs we can accept.
#define AP_NB 50
// Array for storing APs returned by scan_wifi_b component.
static wifi_ap_record_t ap_records[AP_NB];

/**
 * Returns true if the AP defined by OTA_UPDATE_AP_SSID is available.
 */
static bool is_ota_ap_available(uint8_t found_ap_nb) {

    uint8_t ssid_ota_l = strlen(OTA_UPDATE_AP_SSID);
    uint8_t ssid_l;
    for (uint16_t i = 0; i < found_ap_nb; i++) {
        ssid_l = strlen((char *)ap_records[i].ssid);
        if ((ssid_l == ssid_ota_l) &&
            (strncmp((char *)ap_records[i].ssid, OTA_UPDATE_AP_SSID, ssid_l) == 0)) {
            return true;
        }
    }
    return false;

}

void app_main(void)
{

    esp_err_t esp_rs;
    swb_status_t swb_rs;
    cwb_status_t cwb_rs;
    ota_status_t ota_rs;

    // Period of time before restarting in case of fatal error.
    const TickType_t wait_before_restart_period =
            pdMS_TO_TICKS(WAIT_BEFORE_RESTART_PERIOD_MS);

    ESP_LOGI(APP_TAG, "===== esp32-fuota %s =====", OTA_VERSION);

    // Wait a bit before first operation, that's better for test and flash erase.
    vTaskDelay(pdMS_TO_TICKS(WAIT_BEFORE_START_PERIOD_MS));

    //Initialize NVS
    esp_rs = nvs_flash_init();
    if ((esp_rs == ESP_ERR_NVS_NO_FREE_PAGES)
            || (esp_rs == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        esp_rs = nvs_flash_erase();
        if (esp_rs != ESP_OK) {
            ESP_LOGE(APP_TAG, "Error from nvs_flash_erase: %s",
                    esp_err_to_name(esp_rs));
            goto exit_on_fatal_error;
        }
        esp_rs = nvs_flash_init();
        if (esp_rs != ESP_OK) {
            ESP_LOGE(APP_TAG, "Error from nvs_flash_init: %s",
                    esp_err_to_name(esp_rs));
            goto exit_on_fatal_error;
        }
    }

    // Initialize TCP/IP stack.
    esp_rs = esp_netif_init();
    if (esp_rs != ESP_OK) {
        ESP_LOGE(APP_TAG, "Error from esp_netif_init: %s",
                 esp_err_to_name(esp_rs));
        goto exit_on_fatal_error;
    }

    // Create the default event loop.
    esp_rs = esp_event_loop_create_default();
    if (esp_rs != ESP_OK) {
        ESP_LOGE(APP_TAG, "Error from esp_event_loop_create_default: %s",
                 esp_err_to_name(esp_rs));
        goto exit_on_fatal_error;
    }

    state_t current_state = ST_SCAN;

    // Number of APs returned by the scan operation.
    uint8_t found_ap_nb;

    while (true) {

        switch (current_state) {

        case ST_SCAN:
            // Look for AP.
            swb_rs = swb_scan_b(AP_NB, ap_records, &found_ap_nb);
            if (swb_rs == SWB_ERROR) {
                ESP_LOGE(APP_TAG, "Error from swb_scan_b");
                goto exit_on_fatal_error;
            }
            if (swb_rs == SWB_SUCCESS) {
                ESP_LOGI(APP_TAG, "%d APs found", found_ap_nb);
                if (found_ap_nb == 0) {
                    // No open AP available. Stay in same state, wait before next scan.
                    vTaskDelay(pdMS_TO_TICKS(WAIT_BEFORE_NEXT_SCAN_MS));
                    break;
                }
                // At this stage, we have one open AP at least. Check if we
                // have the OTA update AP.
                if (is_ota_ap_available(found_ap_nb)) {
                    ESP_LOGI(APP_TAG, "OTA AP is available");
                    current_state = ST_TRY_OTA;
                    break;
                }
            }
            // At this stage, unexpected return status from swb_scan_b.
            ESP_LOGE(APP_TAG, "Unexpected return status from sw_scan_b: %d", swb_rs);
            goto exit_on_fatal_error;

        case ST_TRY_OTA:
            cwb_rs = cwb_connect_b((uint8_t *)OTA_UPDATE_AP_SSID, (uint8_t *)OTA_UPDATE_AP_PASSWORD,
                                   IP_TIMEOUT_MS);
            if (cwb_rs == CWB_OK) {
                // Connection established with AP.
                ESP_LOGI(APP_TAG, "Connected to AP %s", OTA_UPDATE_AP_SSID);
                ota_rs = ota_update_b(OTA_SERVER_NAME, OTA_SERVER_PORT,
                                      (const char *)server_cert_pem_start,
                                      OTA_SERVER_USERNAME, OTA_SERVER_PASSWORD,
                                      DEV_ID, OTA_VERSION);
                if (ota_rs == OTA_SYS_ERR) {
                    goto exit_on_fatal_error;
                }
                if ((ota_rs == OTA_CONN_ERR) || (ota_rs == OTA_PARAM_ERR) ||
                    (ota_rs == OTA_NO_UPDATE)) {
                    // Possible return status:
                    // - connectivity has been lost
                    // - error in OTA update configuration (local side or server side)
                    // - no update available
                    switch (ota_rs) {
                    case OTA_CONN_ERR:
                        ESP_LOGW(APP_TAG, "Connectivity lost");
                        break;
                    case OTA_PARAM_ERR:
                        ESP_LOGW(APP_TAG, "OTA update configuration error");
                        break;
                    case OTA_NO_UPDATE:
                        ESP_LOGI(APP_TAG, "No update available");
                        break;
                    default:
                        ESP_LOGE(APP_TAG, "Inconsistent value for ota_rs: %d", ota_rs);
                        goto exit_on_fatal_error;
                    }
                    cwb_rs = cwb_disconnect_b();
                    if ((cwb_rs == CWB_OK) || (cwb_rs == CWB_ALREADY_DIS)) {
                        current_state = ST_SCAN;
                        // Wait before next scan.
                        vTaskDelay(pdMS_TO_TICKS(WAIT_BEFORE_NEXT_SCAN_MS));
                        break;
                    }
                    if ((cwb_rs == CWB_DIS_TIMEOUT) || (cwb_rs == CWB_SYS_ERR)) {
                        ESP_LOGE(APP_TAG, "Error on disconnection");
                        goto exit_on_fatal_error;
                    }
                    break;
                }
                if (ota_rs == OTA_UPDATED) {
                    ESP_LOGI(APP_TAG, "Firmware updated, restarting");
                    // Disconnect. We don't test the return status as we restart right after.
                    cwb_disconnect_b();
                    esp_restart();
                }
                // At this stage, unexpected return status from ota_update_b.
                ESP_LOGE(APP_TAG, "Unexpected return status from ota_update_b: %d", ota_rs);
                goto exit_on_fatal_error;
            }
            if ((cwb_rs == CWB_IP_TIMEOUT) || (cwb_rs == CWB_DIS) ||
                                (cwb_rs == CWB_CONN_ERR)) {
                ESP_LOGW(APP_TAG, "Couldn't connect to OTA AP");
                current_state = ST_SCAN;
                // Wait before next scan.
                vTaskDelay(pdMS_TO_TICKS(WAIT_BEFORE_NEXT_SCAN_MS));
                break;
            }
            if ((cwb_rs == CWB_ALREADY_CON) || (cwb_rs == CWB_PARAM_ERR) ||
                (cwb_rs == CWB_SYS_ERR)) {
                ESP_LOGE(APP_TAG, "Weird!");
                goto exit_on_fatal_error;
            }
            // At this stage, unexpected return status from cwb_connect_b.
            ESP_LOGE(APP_TAG, "Unexpected return status from cwb_connect_b: %d", cwb_rs);
            goto exit_on_fatal_error;

        default:
            ESP_LOGE(APP_TAG, "Unknown state: %d", current_state);
            goto exit_on_fatal_error;
        }

    }  // while (true)

    exit_on_fatal_error:
    ESP_LOGI(APP_TAG, "Waiting before restarting...");
    vTaskDelay(wait_before_restart_period);
    ESP_LOGI(APP_TAG, "Restarting...");
    esp_restart();

}
