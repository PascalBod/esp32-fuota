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

#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "fuota_b.h"

const char OTA_TAG[] = "OTA";

static const char VER_PARAM[] = "app_ver";
static const char HTTPS[] = "https://";
static const char DEVICES_PATH[] = "/devices";
static const char FILES_PATH[] = "/files";

// Buffer for update file path, including final '\0'.
#define UPDATE_FILE_PATH_MAX_LENGTH 255
static char update_file_path[UPDATE_FILE_PATH_MAX_LENGTH + 1];
// Buffer for the request URLs, including final '\0'.
#define REQUEST_URL_MAX_LENGTH 512
static char request_url[REQUEST_URL_MAX_LENGTH + 1];

// Stops the communication with the server, deallocating resources.
// Returned value:
// - OTA_OK
// - OTA_CONN_ERR
// - OTA_SYS_ERR
static ota_status_t stop_comm(esp_http_client_handle_t client) {

    esp_err_t esp_rs;

    esp_rs = esp_http_client_close(client);
    if (esp_rs != ESP_OK) {
        ESP_LOGE(OTA_TAG, "stop_comm - esp_http_client_close failed");
        return OTA_CONN_ERR;
    }
    esp_rs = esp_http_client_cleanup(client);
    if (esp_rs != ESP_OK) {
        ESP_LOGE(OTA_TAG, "stop_comm - esp_http_client_cleanup failed");
        return OTA_SYS_ERR;
    }
    return OTA_OK;

}

/**
 * Event handler used by esp_https_ota().
 */
esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    static uint32_t data_len = 0;

    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGI(OTA_TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(OTA_TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADERS_SENT:
        ESP_LOGI(OTA_TAG, "HTTP_EVENT_HEADERS_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGI(OTA_TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        data_len += evt->data_len;
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(OTA_TAG, "HTTP_EVENT_ON_FINISH - data length: %u", data_len);
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(OTA_TAG, "HTTP_EVENT_DISCONNECTED - data length: %u", data_len);
        break;
    default:
        ESP_LOGE(OTA_TAG, "http_event_handler - Unexpected event ID: %d", evt->event_id);
    }
    return ESP_OK;
}

ota_status_t ota_update_b(const char *server_name, uint16_t server_port,
                          const char *cert_pem, const  char *username,
                          const char *password,
                          const char *id,
                          const char *app_ver) {

    esp_err_t esp_rs;
    ota_status_t ota_rs;

    esp_http_client_config_t config = {0};
    esp_http_client_handle_t client;

    ESP_LOGI(OTA_TAG, "Starting update with %s:%d", server_name,
             server_port);
    // Check whether an update is available.
    // First, build the request URL: https://<server_name>:<server_port><path>?<query>.
    // Path and query: /devices/<device_id>?app_ver=<app_version>.
    // The assignment below allows to check that the resulting URL will not be too long
    // for the buffer.
    int url_length = snprintf(NULL, 0, "%s%s:%d%s/%s?%s=%s",
                              HTTPS, server_name, server_port,
                              DEVICES_PATH, id,
                              VER_PARAM, app_ver);
    if (url_length > REQUEST_URL_MAX_LENGTH) {
        ESP_LOGE(OTA_TAG, "Request too long, exiting");
        return OTA_PARAM_ERR;
    }
    snprintf(request_url, REQUEST_URL_MAX_LENGTH, "%s%s:%d%s/%s?%s=%s",
              HTTPS, server_name, server_port,
              DEVICES_PATH, id,
              VER_PARAM, app_ver);
    config.url = request_url;
    config.method = HTTP_METHOD_GET;
    config.cert_pem = (char *)cert_pem;
    config.event_handler = http_event_handler;
    config.auth_type =  HTTP_AUTH_TYPE_BASIC;
    config.username = username;
    config.password = password;
    client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(OTA_TAG, "esp_http_client error, exiting");
        return OTA_SYS_ERR;
    }
    // We don't have any content to send: write_len is 0.
    esp_rs = esp_http_client_open(client, 0);
    if (esp_rs != ESP_OK) {
        ESP_LOGE(OTA_TAG, "esp_http_client_open error, exiting");
        return OTA_CONN_ERR;
    }
    int content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI(OTA_TAG, "Content length: %d", content_length);
    if (content_length == ESP_FAIL) {
        ESP_LOGE(OTA_TAG, "esp_http_client_fetch_headers error, exiting");
        return OTA_CONN_ERR;
    }
    // We do not test content_length against 0, as it can be 0 when
    // no update is available. This case is handled by status code 204 below.
    int status_code = esp_http_client_get_status_code(client);
    if (status_code == 400) {
        ESP_LOGW(OTA_TAG, "Bad Request");
        ota_rs = stop_comm(client);
        if (ota_rs != OTA_OK) {
            return ota_rs;
        }
        return OTA_PARAM_ERR;
    }
    if (status_code == 403) {
        ESP_LOGW(OTA_TAG, "Forbidden");
        ota_rs = stop_comm(client);
        if (ota_rs != OTA_OK) {
            return ota_rs;
        }
        return OTA_PARAM_ERR;
    }
    if (status_code == 404) {
        ESP_LOGW(OTA_TAG, "Not Found");
        ota_rs = stop_comm(client);
        if (ota_rs != OTA_OK) {
            return ota_rs;
        }
        return OTA_NO_UPDATE;
    }
    if (status_code == 204) {
        ESP_LOGI(OTA_TAG, "No Content");
        ota_rs = stop_comm(client);
        if (ota_rs != OTA_OK) {
            return ota_rs;
        }
        return OTA_NO_UPDATE;
    }
    if (status_code == 200) {
        ESP_LOGI(OTA_TAG, "OK");
        if (content_length > UPDATE_FILE_PATH_MAX_LENGTH) {
            // We don't have enough space to store returned content. Abort.
            ESP_LOGE(OTA_TAG, "Content_length too large: %d",
                     content_length);
            stop_comm(client);
            return OTA_PARAM_ERR;
        }
        // At this stage, we can store received content. So, get it.
        esp_http_client_read(client, update_file_path, content_length);
        // And stop communication with the server.
        ota_rs = stop_comm(client);
        if (ota_rs != OTA_OK) {
            return ota_rs;
        }
        // At this stage, update is supposed to be available, download and
        // flash it.
        ESP_LOGI(OTA_TAG, "Requesting %s", update_file_path);
        // URL: https://<server_name>:<server_port><path>.
        // Path: /files/<update_file_path>.
        url_length = snprintf(NULL, 0, "%s%s:%d%s/%s",
                              HTTPS, server_name, server_port,
                              FILES_PATH,
                              update_file_path);
        if (url_length > REQUEST_URL_MAX_LENGTH) {
            ESP_LOGE(OTA_TAG, "Request too long, exiting");
            return OTA_PARAM_ERR;
        }
        snprintf(request_url, REQUEST_URL_MAX_LENGTH, "%s%s:%d%s/%s",
                  HTTPS, server_name, server_port,
                  FILES_PATH,
                  update_file_path);
        config.url = request_url;
        esp_rs = esp_https_ota(&config);
        if (esp_rs != ESP_OK) {
            ESP_LOGE(OTA_TAG, "Update error: %s - Exiting",
                     esp_err_to_name(esp_rs));
            // We return a connectivity error, even if it could be something
            // else.
            return OTA_CONN_ERR;
        }
        // At this stage, update OK.
        ESP_LOGI(OTA_TAG, "Update successful");
        return OTA_UPDATED;
    }
    // At this stage, unexpected status code.
    ESP_LOGE(OTA_TAG, "Unexpected status code: %d - Exiting",
             status_code);
    return OTA_SYS_ERR;

}

