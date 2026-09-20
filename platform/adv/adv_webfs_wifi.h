#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif.h"

typedef struct {
    esp_netif_t *netif;
    bool event_loop;
    bool initialized;
    bool started;
    bool entropy_rx;
    char ssid[15];
    char password[9];
} webfs_wifi_t;

esp_err_t webfs_wifi_start(webfs_wifi_t *wifi);
void webfs_wifi_stop(webfs_wifi_t *wifi);
