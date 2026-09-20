#include "adv_webfs_http.h"
#include "adv_webfs_page.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#if CONFIG_HTTPD_MAX_URI_LEN < WEBFS_QUERY_CAP + 16
#error "WebFS requires HTTPD_MAX_URI_LEN >= WEBFS_QUERY_CAP + 16"
#endif

/* Check cancellation below the request parser as well as in streaming loops:
 * a client trickling headers/body must not keep Q/Esc inside httpd_stop. The
 * server's two-second socket timeout bounds an already-blocked send/receive. */
static int socket_receive(httpd_handle_t server, int fd, char *data, size_t size, int flags)
{
    webfs_http_t *http = httpd_get_global_user_ctx(server);
    if (atomic_load(&http->stopping)) return HTTPD_SOCK_ERR_FAIL;
    int count = recv(fd, data, size, flags);
    if (count >= 0) return count;
    return errno == EAGAIN || errno == EWOULDBLOCK ? HTTPD_SOCK_ERR_TIMEOUT : HTTPD_SOCK_ERR_FAIL;
}

static int socket_send(httpd_handle_t server, int fd, const char *data, size_t size, int flags)
{
    webfs_http_t *http = httpd_get_global_user_ctx(server);
    if (atomic_load(&http->stopping)) return HTTPD_SOCK_ERR_FAIL;
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

typedef struct {
    httpd_req_t *req;
    bool sent;
    bool download;
} response_t;

static bool send_chunk(void *ctx, const char *data, size_t length)
{
    response_t *response = ctx;
    webfs_http_t *http = response->req->user_ctx;
    if (atomic_load(&http->stopping)) return false;
    if (!length) return true;
    if (!response->sent && response->download)
        httpd_resp_set_hdr(response->req, "Content-Disposition", "attachment");
    response->sent = true;
    return httpd_resp_send_chunk(response->req, data, length) == ESP_OK;
}

static esp_err_t api_error(httpd_req_t *req, mini_result_t rc)
{
    const char *status = "500 Internal Server Error";
    const char *message = "Filesystem I/O error";
    if (rc == MINI_ERR_INVALID || rc == MINI_ERR_NAME_TOO_LONG) {
        status = "400 Bad Request"; message = "Invalid or too long path";
    } else if (rc == MINI_ERR_NOT_FOUND) {
        status = "404 Not Found"; message = "Path or volume not found";
    } else if (rc == MINI_ERR_NOT_READY) {
        status = "503 Service Unavailable"; message = "Volume unavailable";
    } else if (rc == MINI_ERR_NOT_DIR || rc == MINI_ERR_IS_DIR) {
        status = "400 Bad Request"; message = "Wrong file type";
    } else if (rc == MINI_ERR_ACCESS) {
        status = "403 Forbidden"; message = "Access denied";
    }
    char body[128];
    snprintf(body, sizeof(body), "{\"error\":\"%s (%ld)\"}", message, (long)rc);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_request(httpd_req_t *req)
{
    webfs_http_t *http = req->user_ctx;
    if (atomic_load(&http->stopping)) return ESP_FAIL;
    if (req->content_len) {
        (void)api_error(req, MINI_ERR_INVALID);
        return ESP_FAIL;
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    size_t route_length = strcspn(req->uri, "?");
    if (route_length == 1 && req->uri[0] == '/') {
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        return httpd_resp_send(req, webfs_page, sizeof(webfs_page)-1);
    }
    if (route_length == 12 && !strncmp(req->uri, "/favicon.ico", 12)) {
        httpd_resp_set_status(req, "204 No Content");
        return httpd_resp_send(req, NULL, 0);
    }
    size_t length = httpd_req_get_url_query_len(req);
    if (!length || length >= sizeof(http->buffers.query) ||
        httpd_req_get_url_query_str(req, http->buffers.query, sizeof(http->buffers.query)) != ESP_OK ||
        !webfs_query_path(http->buffers.query, http->buffers.path, sizeof(http->buffers.path)))
        return api_error(req, MINI_ERR_INVALID);

    bool listing = strncmp(req->uri, "/api/list?", 10) == 0;
    response_t response = {.req = req, .download = !listing};
    const mini_api_t *api = mini_api_get();
    httpd_resp_set_type(req, listing ? "application/json; charset=utf-8" : "application/octet-stream");
    mini_result_t rc;
    if (listing) rc = webfs_list(api->fs, &http->buffers, send_chunk, &response);
    else {
        rc = webfs_file(api->fs, &http->buffers, send_chunk, &response);
    }
    if (rc != MINI_OK) {
        /* A failed stream must not receive the success terminator. Closing the
         * session makes truncated JSON/downloads observable by the browser. */
        if (response.sent || atomic_load(&http->stopping)) return ESP_FAIL;
        return api_error(req, rc);
    }
    if (atomic_load(&http->stopping)) return ESP_FAIL;
    if (!listing && !response.sent)
        httpd_resp_set_hdr(req, "Content-Disposition", "attachment");
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t webfs_http_start(webfs_http_t *http)
{
    atomic_init(&http->stopping, false);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = WEBFS_HTTP_STACK;
    config.open_fn = socket_open;
    config.global_user_ctx = http;
    config.global_user_ctx_free_fn = context_borrowed;
    config.max_open_sockets = 2;
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 2;
    config.send_wait_timeout = 2;
    esp_err_t rc = httpd_start(&http->server, &config);
    if (rc != ESP_OK) return rc;
    const char *paths[] = {"/", "/api/list", "/api/file", "/favicon.ico"};
    for (size_t i = 0; i < sizeof(paths)/sizeof(paths[0]); ++i) {
        httpd_uri_t uri = {.uri = paths[i], .method = HTTP_GET,
                          .handler = handle_request, .user_ctx = http};
        rc = httpd_register_uri_handler(http->server, &uri);
        if (rc != ESP_OK) return rc;
    }
    return ESP_OK;
}

void webfs_http_stop(webfs_http_t *http)
{
    atomic_store(&http->stopping, true);
    if (!http->server) return;
    /* The stop control message can fail transiently. Never free app-scoped
     * state or return to app_end while the HTTP task still exists. */
    while (httpd_stop(http->server) != ESP_OK) {
        ESP_LOGE("webfs", "HTTP stop failed; retrying before app cleanup");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    http->server = NULL;
}
