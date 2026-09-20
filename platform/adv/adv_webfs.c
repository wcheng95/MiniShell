#include "minishell/api.h"
#include "adv_webfs_http.h"
#include "adv_webfs_wifi.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static void line(const mini_api_t *api, uint32_t row, const char *text)
{
    size_t n = strlen(text);
    if (n > 20) n = 20;
    api->display->text->write_at(row, 0, text, (uint32_t)n);
}

static void heap_report(const mini_api_t *api, const char *phase)
{
    char text[160];
    snprintf(text, sizeof(text), "webfs %s: free=%u largest=%u minimum=%u app-stack-free=%u\n",
             phase, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    api->system->write(text);
}

int minishell_app_webfs_main(int argc, char **argv)
{
    (void)argv;
    const mini_api_t *api = mini_api_get();
    if (argc != 1) { api->console->write("usage: webfs\n"); return 1; }
    heap_report(api, "before");
    webfs_http_t *http = NULL;
    if (api->memory->alloc(sizeof(*http), (void **)&http) != MINI_OK) {
        api->console->write("webfs: no memory\n"); return 1;
    }
    memset(http, 0, sizeof(*http));
    atomic_init(&http->stopping, false);
    webfs_wifi_t wifi = {0};
    api->display->text->clear();
    line(api, 0, "WebFS starting...");
    api->display->present();
    webfs_credentials_t credentials;
    bool configured = webfs_settings_load(api->fs, &credentials);
    esp_err_t rc = webfs_wifi_start(&wifi, configured ? &credentials : NULL);
    memset(&credentials, 0, sizeof(credentials));
    if (rc == ESP_OK) rc = webfs_http_start(http);
    if (rc == ESP_OK) {
        heap_report(api, "active");
        api->display->text->clear();
        if (strlen(wifi.ssid) <= 20 && strlen(wifi.password) <= 17) {
            line(api, 0, "WebFS");
            line(api, 1, wifi.ssid);
            char password[21];
            snprintf(password, sizeof(password), "PW %.17s", wifi.password);
            line(api, 2, password);
            memset(password, 0, sizeof(password));
            line(api, 3, "http://192.168.4.1/");
            line(api, 4, "Browse /flash /sd");
            line(api, 5, "Q / Esc = stop");
        } else {
            /* Maximum credentials occupy six rows; keep the URL/exit hint visible. */
            char text[WEBFS_PASSWORD_CAP + 3];
            snprintf(text, sizeof(text), "SSID %s", wifi.ssid);
            uint32_t row = 0;
            for (size_t offset = 0; offset < strlen(text); offset += 20)
                line(api, row++, text + offset);
            snprintf(text, sizeof(text), "PW %s", wifi.password);
            for (size_t offset = 0; offset < strlen(text); offset += 20)
                line(api, row++, text + offset);
            memset(text, 0, sizeof(text));
            line(api, 6, "192.168.4.1 Q/Esc");
        }
        api->display->present();
        for (;;) {
            mini_key_event_t event = {.struct_size = sizeof(event)};
            mini_result_t result = api->input->key->read(&event, 100);
            if (result == MINI_OK &&
                ((event.type == MINI_KEY_EVENT_CHAR && (event.codepoint == 'q' || event.codepoint == 'Q')) ||
                 (event.type == MINI_KEY_EVENT_SPECIAL && event.key == MINI_KEY_ESCAPE))) break;
            if (result != MINI_OK && result != MINI_ERR_TIMEOUT && result != MINI_ERR_NOT_READY) {
                rc = ESP_FAIL; break;
            }
        }
    }
    api->display->text->clear();
    line(api, 0, "WebFS stopping...");
    api->display->present();
    webfs_http_stop(http);
    webfs_wifi_stop(&wifi);
    api->memory->free(http);
    /* Let terminated RTOS tasks reach idle cleanup before the comparison. */
    api->time_location->sleep_ms(100);
    heap_report(api, "stopped");
    api->display->text->clear();
    api->display->present();
    if (rc != ESP_OK) {
        char error[80];
        snprintf(error, sizeof(error), "webfs: %s\n", esp_err_to_name(rc));
        api->console->write(error);
        return 1;
    }
    api->console->write("webfs: stopped\n");
    return 0;
}
