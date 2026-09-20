#include "adv_webfs_wifi.h"
#include "adv_webfs_logic.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT || !CONFIG_LWIP_DHCPS
#error "WebFS requires SoftAP and DHCP server support"
#endif

/* IDF does not support lwIP deinit. Initialize once on first use, but release
 * every session's netif, event loop, Wi-Fi driver and HTTP task on exit. */
static bool tcpip_ready;

esp_err_t webfs_wifi_start(webfs_wifi_t *wifi)
{
    esp_err_t rc;
    if (!tcpip_ready) {
        rc = esp_netif_init();
        if (rc != ESP_OK) return rc;
        tcpip_ready = true;
    }
    rc = esp_event_loop_create_default();
    if (rc != ESP_OK) return rc;
    wifi->event_loop = true;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = 0;
    rc = esp_wifi_init(&init);
    if (rc != ESP_OK) return rc;
    wifi->initialized = true;
    rc = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (rc != ESP_OK) return rc;
    rc = esp_wifi_set_mode(WIFI_MODE_AP);
    if (rc != ESP_OK) return rc;

    /* Enable RF entropy before generating credentials, without broadcasting an
     * AP or borrowing the battery ADC. IDF documents promiscuous enable as a
     * hardware initialization trigger. No receive callback is installed. */
    rc = esp_wifi_set_promiscuous(true);
    if (rc != ESP_OK) return rc;
    wifi->entropy_rx = true;
    uint8_t random[14];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(wifi->password) - 1u; ++i) {
        uint8_t sample = random[i];
        while (!(wifi->password[i] = webfs_password_letter(sample)))
            esp_fill_random(&sample, sizeof(sample));
    }
    wifi->password[8] = 0;
    rc = esp_wifi_set_promiscuous(false);
    if (rc != ESP_OK) return rc;
    wifi->entropy_rx = false;
    snprintf(wifi->ssid, sizeof(wifi->ssid), "MiniShell-%02X%02X", random[12], random[13]);
    memset(random, 0, sizeof(random));

    esp_netif_config_t net = ESP_NETIF_DEFAULT_WIFI_AP();
    wifi->netif = esp_netif_new(&net);
    if (!wifi->netif) return ESP_ERR_NO_MEM;
    rc = esp_netif_attach_wifi_ap(wifi->netif);
    if (rc != ESP_OK) return rc;
    rc = esp_wifi_set_default_wifi_ap_handlers();
    if (rc != ESP_OK) return rc;
    /* The default AP netif provides 192.168.4.1/24 and its DHCP server. */
    esp_netif_ip_info_t ip;
    rc = esp_netif_get_ip_info(wifi->netif, &ip);
    if (rc != ESP_OK) return rc;
    if (ip.ip.addr != ESP_IP4TOADDR(192, 168, 4, 1)) return ESP_ERR_INVALID_STATE;
    wifi_config_t config = {0};
    memcpy(config.ap.ssid, wifi->ssid, strlen(wifi->ssid));
    config.ap.ssid_len = strlen(wifi->ssid);
    memcpy(config.ap.password, wifi->password, sizeof(wifi->password));
    config.ap.channel = 1;
    config.ap.max_connection = 1;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    rc = esp_wifi_set_config(WIFI_IF_AP, &config);
    memset(&config, 0, sizeof(config));
    if (rc != ESP_OK) return rc;
    rc = esp_wifi_start();
    if (rc == ESP_OK) wifi->started = true;
    return rc;
}

void webfs_wifi_stop(webfs_wifi_t *wifi)
{
    if (wifi->entropy_rx) {
        while (esp_wifi_set_promiscuous(false) != ESP_OK) {
            ESP_LOGE("webfs", "RF entropy stop failed; retrying");
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        wifi->entropy_rx = false;
    }
    if (wifi->started) {
        while (esp_wifi_stop() != ESP_OK) {
            ESP_LOGE("webfs", "Wi-Fi stop failed; retrying");
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        wifi->started = false;
    }
    if (wifi->initialized) {
        while (esp_wifi_deinit() != ESP_OK) {
            ESP_LOGE("webfs", "Wi-Fi deinit failed; retrying");
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        wifi->initialized = false;
    }
    if (wifi->netif) {
        esp_netif_destroy_default_wifi(wifi->netif);
        wifi->netif = NULL;
    }
    if (wifi->event_loop) {
        for (;;) {
            esp_err_t rc = esp_event_loop_delete_default();
            if (rc == ESP_OK) break;
            if (rc == ESP_ERR_INVALID_STATE) {
                /* IDF reports this only when no default loop exists. */
                ESP_LOGW("webfs", "Default event loop already absent");
                break;
            }
            ESP_LOGE("webfs", "Event loop delete failed (%s); retrying", esp_err_to_name(rc));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        wifi->event_loop = false;
    }
    memset(wifi->password, 0, sizeof(wifi->password));
}
