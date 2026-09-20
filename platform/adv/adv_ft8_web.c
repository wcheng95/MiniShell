#include "adv_ft8_web.h"
#include "adv_ft8_web_io.h"
#include "adv_ft8_web_page.h"
#include "adv_webfs_wifi.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <stdatomic.h>
#include <string.h>

#define MIRROR_HTTP_STACK 6144u

typedef struct {
    webfs_wifi_t wifi;
    httpd_handle_t server;
    atomic_bool stopping;
    unsigned polls;
} mirror_t;

static void memory_report(const char *phase)
{
    const unsigned caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI("ft8-web", "%s: internal free=%u largest=%u minimum=%u current-stack-free=%u",
             phase, (unsigned)heap_caps_get_free_size(caps),
             (unsigned)heap_caps_get_largest_free_block(caps),
             (unsigned)heap_caps_get_minimum_free_size(caps),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

/* Bound slow headers/clients during stop just as in the standalone WebFS server. */
static int socket_receive(httpd_handle_t server, int fd, char *data, size_t size, int flags)
{
    mirror_t *mirror = httpd_get_global_user_ctx(server);
    if (atomic_load(&mirror->stopping)) return HTTPD_SOCK_ERR_FAIL;
    int count = recv(fd, data, size, flags);
    if (count >= 0) return count;
    return errno == EAGAIN || errno == EWOULDBLOCK ? HTTPD_SOCK_ERR_TIMEOUT : HTTPD_SOCK_ERR_FAIL;
}

static int socket_send(httpd_handle_t server, int fd, const char *data, size_t size, int flags)
{
    mirror_t *mirror = httpd_get_global_user_ctx(server);
    if (atomic_load(&mirror->stopping)) return HTTPD_SOCK_ERR_FAIL;
    int count = send(fd, data, size, flags);
    if (count >= 0) return count;
    return errno == EAGAIN || errno == EWOULDBLOCK ? HTTPD_SOCK_ERR_TIMEOUT : HTTPD_SOCK_ERR_FAIL;
}

static esp_err_t socket_open(httpd_handle_t server, int fd)
{
    esp_err_t rc = httpd_sess_set_recv_override(server, fd, socket_receive);
    return rc == ESP_OK ? httpd_sess_set_send_override(server, fd, socket_send) : rc;
}

static void context_borrowed(void *ctx) { (void)ctx; }

static esp_err_t handle_request(httpd_req_t *req)
{
    mirror_t *mirror = req->user_ctx;
    if (atomic_load(&mirror->stopping)) return ESP_FAIL;
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    if (req->content_len) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No request body allowed");
        return ESP_FAIL;
    }
    if (req->method == HTTP_PUT) {
        char query[ADV_MIRROR_QUERY_CAP];
        mini_key_event_t event;
        size_t length = httpd_req_get_url_query_len(req);
        if (!length || length >= sizeof(query) ||
            httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
            !adv_mirror_parse_key(query, &event))
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid key");
        mini_result_t rc = adv_remote_input_push(&event);
        if (rc != MINI_OK) {
            httpd_resp_set_status(req, rc == MINI_ERR_NO_SPACE ? "429 Too Many Requests" : "503 Service Unavailable");
            return httpd_resp_send(req, "Key queue unavailable; key not sent", HTTPD_RESP_USE_STRLEN);
        }
        return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
    }
    if (req->uri[1] == 0 || req->uri[1] == '?') {
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        return httpd_resp_send(req, adv_ft8_web_page, sizeof(adv_ft8_web_page) - 1);
    }
    adv_display_snapshot_t snapshot;
    uint8_t payload[ADV_MIRROR_PAYLOAD];
    adv_display_snapshot(&snapshot);
    adv_mirror_encode_screen(&snapshot, payload);
    httpd_resp_set_type(req, "application/octet-stream");
    /* Periodic debug-UART evidence includes the live-RX/decode interval without
     * querying FT8 internals or writing diagnostics into its physical display. */
    if (mirror->polls++ % 60u == 0) memory_report("screen poll (HTTP task)");
    return httpd_resp_send(req, (const char *)payload, sizeof(payload));
}

static esp_err_t mirror_start(mirror_t *mirror)
{
    webfs_credentials_t credentials;
    bool configured = webfs_settings_load(mini_api_get()->fs, &credentials);
    esp_err_t rc = webfs_wifi_start(&mirror->wifi, configured ? &credentials : NULL);
    memset(&credentials, 0, sizeof(credentials));
    if (rc != ESP_OK) return rc;
    if (!adv_remote_input_start()) return ESP_ERR_NO_MEM;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = MIRROR_HTTP_STACK;
    config.task_priority = tskIDLE_PRIORITY + 1;
    config.max_open_sockets = 2;
    config.max_uri_handlers = 3;
    config.recv_wait_timeout = 2;
    config.send_wait_timeout = 2;
    config.lru_purge_enable = true;
    config.open_fn = socket_open;
    config.global_user_ctx = mirror;
    config.global_user_ctx_free_fn = context_borrowed;
    rc = httpd_start(&mirror->server, &config);
    if (rc != ESP_OK) return rc;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = handle_request, .user_ctx = mirror},
        {.uri = "/api/screen", .method = HTTP_GET, .handler = handle_request, .user_ctx = mirror},
        {.uri = "/api/key", .method = HTTP_PUT, .handler = handle_request, .user_ctx = mirror}
    };
    for (size_t i = 0; i < sizeof(routes)/sizeof(routes[0]); ++i) {
        rc = httpd_register_uri_handler(mirror->server, &routes[i]);
        if (rc != ESP_OK) return rc;
    }
    return ESP_OK;
}

static void mirror_stop(mirror_t *mirror)
{
    atomic_store(&mirror->stopping, true);
    if (mirror->server) {
        while (httpd_stop(mirror->server) != ESP_OK) {
            ESP_LOGE("ft8-web", "HTTP stop failed; retrying before cleanup");
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        mirror->server = NULL;
    }
    adv_remote_input_stop();
    webfs_wifi_stop(&mirror->wifi);
}

int adv_ft8_web_run(int argc, char **argv, int (*entry)(int, char **))
{
    mirror_t mirror = {0};
    atomic_init(&mirror.stopping, false);
    memory_report("before mirror");
    esp_err_t rc = mirror_start(&mirror);
    if (rc != ESP_OK) {
        mirror_stop(&mirror);
        ESP_LOGW("ft8-web", "Mirror unavailable (%s); continuing local FT8", esp_err_to_name(rc));
    } else memory_report("mirror active, before FT8/QMX");
    int result = entry(argc, argv);
    mirror_stop(&mirror);
    vTaskDelay(pdMS_TO_TICKS(100));
    memory_report("stopped");
    return result;
}
