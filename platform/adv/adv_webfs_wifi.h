#pragma once
#include <stdbool.h>
#include "adv_webfs_settings.h"
#include "esp_err.h"
#include "esp_netif.h"

typedef struct {
    esp_netif_t *netif;
    bool event_loop;
    bool initialized;
    bool started;
    bool entropy_rx;
    char ssid[WEBFS_SSID_CAP];
    char password[WEBFS_PASSWORD_CAP];
} webfs_wifi_t;

esp_err_t webfs_wifi_start(webfs_wifi_t *wifi, const webfs_credentials_t *credentials);
void webfs_wifi_stop(webfs_wifi_t *wifi);
