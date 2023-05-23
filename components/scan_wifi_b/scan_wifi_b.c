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

#include "esp_log.h"
#include "esp_wifi.h"

#include "scan_wifi_b.h"

const char SWB_TAG[] = "SWB";

// Wi-Fi scan configuration.
static const wifi_active_scan_time_t active_scan_time = {
        .min = 0,
        .max = 200
};
static const wifi_scan_time_t scan_time = {
        .active = active_scan_time,
        .passive = 0
};
static const wifi_scan_config_t scan_config = {
        .ssid = NULL,           // Scan all SSIDs.
        .bssid = NULL,          // Scan all BSSIDs.
        .channel = 0,           // Scan all channels.
        .show_hidden = false,   // Do not report hidden APs.
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = scan_time
};

static const bool BLOCK = true;

// ESP-NETIF instance.
static esp_netif_t *netif_instance;

swb_status_t swb_scan_b(uint8_t ap_nb, wifi_ap_record_t *ap_records,
                        uint8_t *found_ap_nb) {

    esp_err_t esp_rs;   // Return status for ESP-IDF calls.

    netif_instance = esp_netif_create_default_wifi_sta();
    if (netif_instance == NULL) {
        ESP_LOGE(SWB_TAG, "Error from esp_netif_create_default_wifi_sta");
        return SWB_ERROR;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_rs = esp_wifi_init(&cfg);
    if (esp_rs != ESP_OK) {
        ESP_LOGE(SWB_TAG, "Error from esp_wifi_init: %s", esp_err_to_name(esp_rs));
        return SWB_ERROR;
    }

    esp_rs = esp_wifi_set_mode(WIFI_MODE_STA);
    if (esp_rs != ESP_OK) {
        ESP_LOGE(SWB_TAG, "Error from esp_wifi_set_mode: %s", esp_err_to_name(esp_rs));
        esp_wifi_deinit();
        return SWB_ERROR;
    }

    // At this stage, Wi-Fi initialization is OK.
    esp_rs = esp_wifi_start();
    if (esp_rs != ESP_OK) {
        ESP_LOGE(SWB_TAG, "Error from esp_wifi_start: %s", esp_err_to_name(esp_rs));
        return SWB_ERROR;
    }

    esp_rs = esp_wifi_scan_start(&scan_config, BLOCK);
    if (esp_rs != ESP_OK) {
        ESP_LOGE(SWB_TAG, "Error from esp_wifi_scan_start: %s",
                 esp_err_to_name(esp_rs));
        esp_wifi_deinit();
        esp_netif_destroy(netif_instance);
        return SWB_ERROR;
    }

    // Number of APs must be 16 bits, for esp_wifi_scan_get_ap_records.
    uint16_t ap_nb_16 = ap_nb;
    esp_rs = esp_wifi_scan_get_ap_records(&ap_nb_16, ap_records);
    if (esp_rs != ESP_OK) {
        ESP_LOGE(SWB_TAG, "Error from esp_wifi_scan_get_ap_records: %s",
                 esp_err_to_name(esp_rs));
        esp_wifi_deinit();
        esp_netif_destroy(netif_instance);
        return SWB_ERROR;
    }
    // Could be that next call is required, to release memory.
    uint16_t ap_count;
    esp_rs = esp_wifi_scan_get_ap_num(&ap_count);
    if (esp_rs != ESP_OK) {
        ESP_LOGE(SWB_TAG, "Error from esp_wifi_scan_get_ap_num: %s",
                 esp_err_to_name(esp_rs));
        esp_wifi_deinit();
        esp_netif_destroy(netif_instance);
        return SWB_ERROR;
    }

    // Stop Wi-Fi.
    esp_rs = esp_wifi_stop();
    if (esp_rs != ESP_OK) {
        ESP_LOGE(SWB_TAG, "Error from esp_wifi_stop: %s",
                 esp_err_to_name(esp_rs));
        esp_wifi_deinit();
        esp_netif_destroy(netif_instance);
        return SWB_ERROR;
    }
    esp_wifi_deinit();
    esp_netif_destroy(netif_instance);

    // Return information.
    if (ap_count > (uint16_t)ap_nb) {
        *found_ap_nb = ap_nb;
    } else {
        *found_ap_nb = ap_count;
    }

    return SWB_SUCCESS;
}
