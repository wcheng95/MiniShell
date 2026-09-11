#include "adv_gps.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "adv_internal.h"
#include "adv_rtc.h"
#include "minishell_services.h"

#define ADV_GPS_UART UART_NUM_1
#define ADV_GPS_RX_GPIO GPIO_NUM_1
#define ADV_GPS_TX_GPIO GPIO_NUM_2
#define ADV_GPS_BAUD_FAST 115200
#define ADV_GPS_BAUD_SLOW 9600
#define ADV_GPS_PROBE_MS 2500
#define ADV_GPS_RX_BUFFER 2048
#define ADV_GPS_READ_CHUNK 256
#define ADV_GPS_LINE_CAP 128
#define ADV_GPS_TASK_STACK 4096
#define ADV_GPS_TASK_PRIORITY 5

#define ADV_STATE_DIR "/flash/minishell"
#define ADV_GPS_BAUD_PATH ADV_STATE_DIR "/gps_baud.txt"
#define ADV_GPS_BAUD_TEMP ADV_STATE_DIR "/gps_baud.tmp"

static const char *TAG = "ADV_GPS";
static TaskHandle_t s_task;
static volatile bool s_running;
static volatile bool s_ready;
static volatile bool s_baud_locked;
static volatile int s_active_baud = ADV_GPS_BAUD_FAST;
static int s_saved_baud = ADV_GPS_BAUD_FAST;
static char s_line[ADV_GPS_LINE_CAP];
static size_t s_line_length;
static int64_t s_probe_start_ms;
static bool s_probe_saw_bytes;
static bool s_rtc_synced_once;
static int s_last_rtc_hour_key = -1;

static int normalize_baud(int baud)
{
    return baud == ADV_GPS_BAUD_SLOW ? ADV_GPS_BAUD_SLOW : ADV_GPS_BAUD_FAST;
}

static int64_t monotonic_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static bool checksum_valid(const char *line)
{
    const char *star;
    uint8_t checksum = 0u;
    int hi;
    int lo;

    if (line == NULL || line[0] != '$') return false;
    star = strchr(line, '*');
    if (star == NULL || star[1] == '\0' || star[2] == '\0') return false;
    for (const char *p = line + 1; p < star; ++p) checksum ^= (uint8_t)*p;
    hi = hex_value(star[1]);
    lo = hex_value(star[2]);
    return hi >= 0 && lo >= 0 && checksum == (uint8_t)((hi << 4) | lo);
}

static bool sentence_suffix_is(const char *field0, const char *suffix)
{
    size_t length;
    if (field0 == NULL || suffix == NULL) return false;
    length = strlen(field0);
    return length >= 3u && strcmp(field0 + length - 3u, suffix) == 0;
}

static size_t split_fields(char *payload, char **fields, size_t capacity)
{
    size_t count = 0u;
    char *start = payload;

    if (payload == NULL || fields == NULL || capacity == 0u) return 0u;
    fields[count++] = start;
    for (char *p = payload; *p != '\0' && count < capacity; ++p) {
        if (*p == ',') {
            *p = '\0';
            fields[count++] = p + 1;
        }
    }
    return count;
}

static bool parse_uint_digits(const char *text, size_t count, unsigned *out)
{
    unsigned value = 0u;
    if (text == NULL || out == NULL) return false;
    for (size_t i = 0u; i < count; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        value = value * 10u + (unsigned)(text[i] - '0');
    }
    *out = value;
    return true;
}

static bool parse_nmea_coordinate_e7(const char *text, char hemisphere,
                                      bool latitude, int32_t *out_e7)
{
    const char *dot;
    size_t integer_digits;
    size_t degree_digits;
    unsigned degrees = 0u;
    unsigned minute_whole = 0u;
    uint64_t minute_fraction = 0u;
    uint64_t fraction_scale = 1u;
    uint64_t minute_scaled;
    uint64_t denominator;
    int64_t value;

    if (text == NULL || out_e7 == NULL || text[0] == '\0') return false;
    dot = strchr(text, '.');
    integer_digits = dot != NULL ? (size_t)(dot - text) : strlen(text);
    if (integer_digits < 3u) return false;
    degree_digits = integer_digits - 2u;
    if ((latitude && degree_digits != 2u) || (!latitude && degree_digits != 3u)) return false;
    if (!parse_uint_digits(text, degree_digits, &degrees) ||
        !parse_uint_digits(text + degree_digits, 2u, &minute_whole)) return false;
    if (minute_whole >= 60u) return false;

    if (dot != NULL) {
        const char *p = dot + 1;
        unsigned digits = 0u;
        while (*p >= '0' && *p <= '9' && digits < 7u) {
            minute_fraction = minute_fraction * 10u + (uint64_t)(*p - '0');
            fraction_scale *= 10u;
            ++p;
            ++digits;
        }
        if (*p != '\0') return false;
    }

    minute_scaled = (uint64_t)minute_whole * fraction_scale + minute_fraction;
    denominator = 60u * fraction_scale;
    value = (int64_t)degrees * 10000000ll +
            (int64_t)((minute_scaled * 10000000ull + denominator / 2u) / denominator);

    if (latitude) {
        if (degrees > 90u || (hemisphere != 'N' && hemisphere != 'S')) return false;
        if (hemisphere == 'S') value = -value;
        if (value < -900000000ll || value > 900000000ll) return false;
    } else {
        if (degrees > 180u || (hemisphere != 'E' && hemisphere != 'W')) return false;
        if (hemisphere == 'W') value = -value;
        if (value < -1800000000ll || value > 1800000000ll) return false;
    }

    *out_e7 = (int32_t)value;
    return true;
}

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2u;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned shifted_month = (unsigned)((int)month + (month > 2u ? -3 : 9));
    unsigned doy = (153u * shifted_month + 2u) / 5u + day - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static bool valid_calendar_date(int year, unsigned month, unsigned day)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned limit;
    bool leap;

    if (year < 2000 || year > 2099 || month < 1u || month > 12u || day < 1u) return false;
    limit = days[month - 1u];
    leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (month == 2u && leap) limit = 29u;
    return day <= limit;
}

static bool parse_rmc_utc(const char *time_text, const char *date_text,
                          int64_t *out_seconds, int *out_hour_key,
                          unsigned *out_minute, unsigned *out_second)
{
    unsigned hour;
    unsigned minute;
    unsigned second;
    unsigned day;
    unsigned month;
    unsigned year2;
    int year;

    if (time_text == NULL || date_text == NULL || out_seconds == NULL ||
        strlen(time_text) < 6u || strlen(date_text) != 6u ||
        !parse_uint_digits(time_text, 2u, &hour) ||
        !parse_uint_digits(time_text + 2u, 2u, &minute) ||
        !parse_uint_digits(time_text + 4u, 2u, &second) ||
        !parse_uint_digits(date_text, 2u, &day) ||
        !parse_uint_digits(date_text + 2u, 2u, &month) ||
        !parse_uint_digits(date_text + 4u, 2u, &year2)) {
        return false;
    }
    if (hour > 23u || minute > 59u || second > 59u) return false;
    year = 2000 + (int)year2;
    if (!valid_calendar_date(year, month, day)) return false;

    *out_seconds = days_from_civil(year, month, day) * 86400ll +
                   (int64_t)hour * 3600ll + (int64_t)minute * 60ll + (int64_t)second;
    if (out_hour_key != NULL) {
        *out_hour_key = ((((year * 100) + (int)month) * 100 + (int)day) * 100 + (int)hour);
    }
    if (out_minute != NULL) *out_minute = minute;
    if (out_second != NULL) *out_second = second;
    return true;
}

static int load_saved_baud(void)
{
    FILE *file;
    int baud = ADV_GPS_BAUD_FAST;

    if (!adv_filesystem_flash_ready()) return baud;
    file = fopen(ADV_GPS_BAUD_PATH, "r");
    if (file == NULL) return baud;
    if (fscanf(file, "%d", &baud) != 1) baud = ADV_GPS_BAUD_FAST;
    (void)fclose(file);
    return normalize_baud(baud);
}

static void persist_baud(int baud)
{
    FILE *file;
    int fd;

    if (!adv_filesystem_flash_ready()) return;
    if (mkdir(ADV_STATE_DIR, 0775) != 0 && errno != EEXIST) return;
    file = fopen(ADV_GPS_BAUD_TEMP, "w");
    if (file == NULL) return;
    if (fprintf(file, "%d\n", baud) < 0 || fflush(file) != 0) {
        (void)fclose(file);
        (void)unlink(ADV_GPS_BAUD_TEMP);
        return;
    }
    fd = fileno(file);
    if (fd >= 0 && fsync(fd) != 0) {
        (void)fclose(file);
        (void)unlink(ADV_GPS_BAUD_TEMP);
        return;
    }
    if (fclose(file) != 0 || rename(ADV_GPS_BAUD_TEMP, ADV_GPS_BAUD_PATH) != 0) {
        (void)unlink(ADV_GPS_BAUD_TEMP);
        return;
    }
    s_saved_baud = baud;
}

static void lock_baud(void)
{
    if (s_baud_locked) return;
    s_baud_locked = true;
    if (s_active_baud != s_saved_baud) persist_baud(s_active_baud);
    ESP_LOGI(TAG, "GPS baud locked: %d", s_active_baud);
}

static void publish_rmc(char **fields, size_t count)
{
    int32_t latitude_e7;
    int32_t longitude_e7;
    int64_t utc_seconds;
    int hour_key = -1;
    unsigned minute = 0u;
    unsigned second = 0u;
    mini_result_t sync_result;

    if (count < 10u) return;
    if (fields[2][0] != 'A') {
        minishell_services_live_location_clear();
        return;
    }
    if (!parse_nmea_coordinate_e7(fields[3], fields[4][0], true, &latitude_e7) ||
        !parse_nmea_coordinate_e7(fields[5], fields[6][0], false, &longitude_e7)) {
        return;
    }

    (void)minishell_services_live_location_update(latitude_e7, longitude_e7);

    if (!parse_rmc_utc(fields[1], fields[9], &utc_seconds, &hour_key, &minute, &second)) return;
    sync_result = minishell_services_utc_sync(utc_seconds, 0u, false);
    if (sync_result != MINI_OK || !adv_rtc_ready()) return;

    if (!s_rtc_synced_once ||
        (minute == 0u && second <= 5u && hour_key >= 0 && hour_key != s_last_rtc_hour_key)) {
        if (minishell_services_utc_sync(utc_seconds, 0u, true) == MINI_OK) {
            s_rtc_synced_once = true;
            s_last_rtc_hour_key = hour_key;
            ESP_LOGI(TAG, "GPS UTC persisted to RTC");
        }
    }
}

static bool process_sentence(const char *line)
{
    char buffer[ADV_GPS_LINE_CAP];
    char *star;
    char *fields[20];
    size_t count;

    if (!checksum_valid(line)) return false;
    if (strlen(line) >= sizeof(buffer)) return false;
    strcpy(buffer, line + 1);
    star = strchr(buffer, '*');
    if (star == NULL) return false;
    *star = '\0';
    count = split_fields(buffer, fields, sizeof(fields) / sizeof(fields[0]));
    if (count == 0u) return false;

    lock_baud();
    if (sentence_suffix_is(fields[0], "RMC")) publish_rmc(fields, count);
    return true;
}

static void reset_probe_window(void)
{
    s_probe_start_ms = monotonic_ms();
    s_probe_saw_bytes = false;
    s_line_length = 0u;
}

static void switch_probe_baud(void)
{
    int next = s_active_baud == ADV_GPS_BAUD_FAST ? ADV_GPS_BAUD_SLOW : ADV_GPS_BAUD_FAST;
    if (uart_set_baudrate(ADV_GPS_UART, (uint32_t)next) == ESP_OK) {
        s_active_baud = next;
        (void)uart_flush_input(ADV_GPS_UART);
        ESP_LOGI(TAG, "GPS probing %d baud", next);
    }
    reset_probe_window();
}

static void consume_byte(uint8_t byte)
{
    if (byte == '\r') return;
    if (byte == '\n') {
        if (s_line_length > 0u) {
            s_line[s_line_length] = '\0';
            (void)process_sentence(s_line);
        }
        s_line_length = 0u;
        return;
    }
    if (byte == '$') s_line_length = 0u;
    if (s_line_length + 1u < sizeof(s_line)) {
        s_line[s_line_length++] = (char)byte;
    } else {
        s_line_length = 0u;
    }
}

static void gps_task(void *arg)
{
    uint8_t data[ADV_GPS_READ_CHUNK];
    (void)arg;
    reset_probe_window();

    while (s_running) {
        int count = uart_read_bytes(ADV_GPS_UART, data, sizeof(data), pdMS_TO_TICKS(100));
        if (count > 0) {
            s_probe_saw_bytes = true;
            for (int i = 0; i < count; ++i) consume_byte(data[i]);
        }
        if (!s_baud_locked && s_probe_saw_bytes &&
            monotonic_ms() - s_probe_start_ms >= ADV_GPS_PROBE_MS) {
            switch_probe_baud();
        }
    }

    s_task = NULL;
    vTaskDelete(NULL);
}

int adv_gps_prepare(void)
{
    uart_config_t config = {
        .baud_rate = ADV_GPS_BAUD_FAST,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (s_ready) return 0;
    if (uart_is_driver_installed(ADV_GPS_UART)) {
        ESP_LOGW(TAG, "UART1 already owned; GPS not started");
        return -1;
    }

    s_saved_baud = load_saved_baud();
    s_active_baud = s_saved_baud;
    config.baud_rate = s_active_baud;

    if (uart_driver_install(ADV_GPS_UART, ADV_GPS_RX_BUFFER, 0, 0, NULL, 0) != ESP_OK) return -1;
    if (uart_param_config(ADV_GPS_UART, &config) != ESP_OK ||
        uart_set_pin(ADV_GPS_UART, ADV_GPS_TX_GPIO, ADV_GPS_RX_GPIO,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        (void)uart_driver_delete(ADV_GPS_UART);
        return -1;
    }

    s_baud_locked = false;
    s_rtc_synced_once = false;
    s_last_rtc_hour_key = -1;
    s_running = true;
    if (xTaskCreate(gps_task, "adv_gps", ADV_GPS_TASK_STACK, NULL,
                    ADV_GPS_TASK_PRIORITY, &s_task) != pdPASS) {
        s_running = false;
        (void)uart_driver_delete(ADV_GPS_UART);
        return -1;
    }
    s_ready = true;
    ESP_LOGI(TAG, "GPS started UART1 RX=G1 TX=G2 at %d baud", s_active_baud);
    return 0;
}

void adv_gps_shutdown(void)
{
    if (!s_ready) return;
    s_running = false;
    if (s_task != NULL) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    (void)uart_driver_delete(ADV_GPS_UART);
    minishell_services_live_location_clear();
    s_ready = false;
    s_baud_locked = false;
}

bool adv_gps_ready(void)
{
    return s_ready;
}

int adv_gps_active_baud(void)
{
    return s_active_baud;
}

bool adv_gps_baud_locked(void)
{
    return s_baud_locked;
}
