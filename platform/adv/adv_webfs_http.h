#pragma once
#include <stdatomic.h>
#include "esp_http_server.h"
#include "adv_webfs_logic.h"

#define WEBFS_HTTP_STACK 6144u

typedef struct {
    webfs_buffers_t buffers;
    atomic_bool stopping;
    httpd_handle_t server;
} webfs_http_t;

esp_err_t webfs_http_start(webfs_http_t *http);
/* Does not return until handlers can no longer access the context or FS. */
void webfs_http_stop(webfs_http_t *http);
