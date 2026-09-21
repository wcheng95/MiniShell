#include "config_service.h"
#include "keyer_text.h"

#include <stddef.h>
#include <stdint.h>

#define KEYER_SETTING_BUFFER_SIZE 1024u

static char ascii_lower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') return (char)(ch - 'A' + 'a');
    return ch;
}

static bool is_space(char ch)
{
    return ch == ' ' || ch == '\t';
}

static bool text_equal_ci(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return false;
    while (*a != '\0' && *b != '\0') {
        if (ascii_lower(*a) != ascii_lower(*b)) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool parse_u32(const char *text, uint32_t *out_value)
{
    uint32_t value = 0u;
    bool any = false;

    if (text == NULL || out_value == NULL) return false;
    while (*text != '\0') {
        if (*text < '0' || *text > '9') return false;
        uint32_t digit = (uint32_t)(*text - '0');
        if (value > (UINT32_MAX - digit) / 10u) return false;
        value = value * 10u + digit;
        any = true;
        ++text;
    }

    if (!any) return false;
    *out_value = value;
    return true;
}

static char *trim(char *text)
{
    char *end;
    if (text == NULL) return NULL;
    while (is_space(*text)) ++text;

    end = text;
    while (*end != '\0') ++end;
    while (end > text && is_space(end[-1])) --end;
    *end = '\0';
    return text;
}

static mini_result_t parse_key_in(const char *value, keyer_key_in_mode_t *out)
{
    if (text_equal_ci(value, "Paddle")) *out = KEYER_KEY_IN_PADDLE;
    else if (text_equal_ci(value, "PaddleR")) *out = KEYER_KEY_IN_PADDLE_R;
    else if (text_equal_ci(value, "SK-T")) *out = KEYER_KEY_IN_SK_T;
    else if (text_equal_ci(value, "SK-R")) *out = KEYER_KEY_IN_SK_R;
    else return MINI_ERR_INVALID;
    return MINI_OK;
}

static mini_result_t parse_paddle(const char *value, keyer_engine_paddle_mode_t *out)
{
    if (text_equal_ci(value, "IambicA")) *out = KEYER_ENGINE_PADDLE_IAMBIC_A;
    else if (text_equal_ci(value, "IambicB")) *out = KEYER_ENGINE_PADDLE_IAMBIC_B;
    else if (text_equal_ci(value, "Bug")) *out = KEYER_ENGINE_PADDLE_BUG;
    else return MINI_ERR_INVALID;
    return MINI_OK;
}

static mini_result_t parse_key_out(const char *value, keyer_key_out_mode_t *out)
{
    if (text_equal_ci(value, "SKS") || text_equal_ci(value, "SK")) *out = KEYER_KEY_OUT_SKS;
    else if (text_equal_ci(value, "SKM") || text_equal_ci(value, "SK-M")) *out = KEYER_KEY_OUT_SKM;
    else if (text_equal_ci(value, "Off")) *out = KEYER_KEY_OUT_OFF;
    else return MINI_ERR_INVALID;
    return MINI_OK;
}

static mini_result_t parse_sidetone_enabled(const char *value, bool *out)
{
    if (text_equal_ci(value, "On")) *out = true;
    else if (text_equal_ci(value, "Off")) *out = false;
    else return MINI_ERR_INVALID;
    return MINI_OK;
}

static mini_result_t parse_line(char *line, keyer_config_t *config)
{
    char *equals;
    char *key;
    char *value;
    uint32_t number;

    while (is_space(*line)) ++line;
    if (line == NULL || *line == '\0' || *line == '#') return MINI_OK;

    equals = line;
    while (*equals != '\0' && *equals != '=') ++equals;
    if (*equals != '=') return MINI_OK;
    *equals = '\0';

    key = trim(line);
    value = equals + 1;
    if (key[0] == 'm' && key[1] >= '1' && key[1] <= '5' && key[2] == 0) {
        size_t n = keyer_text_len(value);
        if (n > 95u) return MINI_ERR_INVALID;
        for (size_t i = 0; i < n; ++i)
            if (value[i] < 32 || value[i] > 126) return MINI_ERR_INVALID;
        keyer_text_copy(config->messages[key[1] - '1'], 96, value);
        return MINI_OK;
    }
    value = trim(value);
    if (key == NULL || value == NULL || *key == '\0' || *value == '\0') {
        return MINI_ERR_INVALID;
    }

    if (text_equal_ci(key, "mute")) return parse_sidetone_enabled(value, &config->mute);
    if (text_equal_ci(key, "volume") || text_equal_ci(key, "repeat_s") ||
        text_equal_ci(key, "tx_delay_s") || text_equal_ci(key, "tune_timeout_s")) {
        if (!parse_u32(value, &number) || number > 99u) return MINI_ERR_INVALID;
        if (text_equal_ci(key, "volume")) config->volume = (uint8_t)number;
        else if (text_equal_ci(key, "repeat_s")) {
            if (!number) return MINI_ERR_INVALID;
            config->repeat_s = (uint8_t)number;
        } else if (text_equal_ci(key, "tx_delay_s")) config->tx_delay_s = (uint8_t)number;
        else {
            if (number > 20u) return MINI_ERR_INVALID;
            config->tune_timeout_s = (uint8_t)number;
        }
        return MINI_OK;
    }
    if (text_equal_ci(key, "wpm")) {
        if (!parse_u32(value, &number) || number < KEYER_ENGINE_MIN_WPM ||
            number > KEYER_ENGINE_MAX_WPM) return MINI_ERR_INVALID;
        config->wpm = (uint8_t)number;
        return MINI_OK;
    }
    if (text_equal_ci(key, "sidetone")) {
        return parse_sidetone_enabled(value, &config->sidetone_enabled);
    }
    if (text_equal_ci(key, "sidetone_hz")) {
        if (!parse_u32(value, &number) || number < KEYER_SIDETONE_MIN_HZ ||
            number > KEYER_SIDETONE_MAX_HZ) return MINI_ERR_INVALID;
        config->sidetone_hz = (uint16_t)number;
        return MINI_OK;
    }
    if (text_equal_ci(key, "key_in")) return parse_key_in(value, &config->key_in_mode);
    if (text_equal_ci(key, "paddle")) return parse_paddle(value, &config->paddle_mode);
    if (text_equal_ci(key, "key_out")) return parse_key_out(value, &config->key_out_mode);

    if (text_equal_ci(key, "key_in_tip_gpio")) {
        if (!parse_u32(value, &number)) return MINI_ERR_INVALID;
        config->key_in_tip_line = number;
        return MINI_OK;
    }
    if (text_equal_ci(key, "key_in_ring_gpio")) {
        if (!parse_u32(value, &number)) return MINI_ERR_INVALID;
        config->key_in_ring_line = number;
        return MINI_OK;
    }
    if (text_equal_ci(key, "key_out_tip_gpio")) {
        if (!parse_u32(value, &number)) return MINI_ERR_INVALID;
        config->key_out_tip_line = number;
        return MINI_OK;
    }
    if (text_equal_ci(key, "key_out_ring_gpio")) {
        if (!parse_u32(value, &number)) return MINI_ERR_INVALID;
        config->key_out_ring_line = number;
        return MINI_OK;
    }

    /* Forward-compatible: unknown settings are ignored by this parser. */
    return MINI_OK;
}

static mini_result_t parse_buffer(char *buffer, keyer_config_t *config)
{
    char *line = buffer;
    char *cursor = buffer;

    while (true) {
        if (*cursor == '\r' || *cursor == '\n' || *cursor == '\0') {
            char terminator = *cursor;
            *cursor = '\0';
            mini_result_t rc = parse_line(line, config);
            if (rc != MINI_OK) return rc;
            if (terminator == '\0') break;

            if (terminator == '\r' && cursor[1] == '\n') ++cursor;
            line = cursor + 1;
        }
        ++cursor;
    }
    return MINI_OK;
}

void config_service_defaults(keyer_config_t *config)
{
    if (config == NULL) return;

    config->wpm = 20u;
    config->volume = 80u;
    config->repeat_s = 10u;
    config->tx_delay_s = 1u;
    config->tune_timeout_s = 10u;
    config->mute = false;
    for (unsigned i = 0; i < 5; ++i)
        for (unsigned j = 0; j < 96; ++j) config->messages[i][j] = 0;
    keyer_text_copy(config->messages[0], 96, "CQ POTA");
    config->sidetone_enabled = true;
    config->sidetone_hz = KEYER_SIDETONE_DEFAULT_HZ;
    config->paddle_mode = KEYER_ENGINE_PADDLE_IAMBIC_A;
    config->key_in_mode = KEYER_KEY_IN_PADDLE;
    config->key_out_mode = KEYER_KEY_OUT_SKS;
    config->key_in_tip_line = 13u;
    config->key_in_ring_line = 15u;
    config->key_out_tip_line = 3u;
    config->key_out_ring_line = 6u;
}

mini_result_t config_service_load(const mini_api_t *api,
                                  keyer_config_t *config,
                                  bool *out_loaded_from_file)
{
    char buffer[KEYER_SETTING_BUFFER_SIZE];
    uint32_t used = 0u;
    mini_file_t file = MINI_FILE_INVALID;
    mini_result_t rc;

    if (config == NULL) return MINI_ERR_INVALID;
    if (out_loaded_from_file != NULL) *out_loaded_from_file = false;
    config_service_defaults(config);

    if (api == NULL || api->fs == NULL || api->fs->open == NULL ||
        api->fs->read == NULL || api->fs->close == NULL) {
        return MINI_ERR_NOT_READY;
    }

    rc = api->fs->open(KEYER_SETTING_PATH, MINI_FS_READ, &file);
    if (rc == MINI_ERR_NOT_FOUND) return MINI_OK;
    if (rc != MINI_OK) return rc;

    while (used + 1u < KEYER_SETTING_BUFFER_SIZE) {
        uint32_t got = 0u;
        rc = api->fs->read(file, &buffer[used],
                           KEYER_SETTING_BUFFER_SIZE - 1u - used, &got);
        if (rc != MINI_OK) {
            (void)api->fs->close(file);
            return rc;
        }
        if (got == 0u) break;
        used += got;
    }

    rc = api->fs->close(file);
    if (rc != MINI_OK) return rc;
    if (used + 1u >= KEYER_SETTING_BUFFER_SIZE) return MINI_ERR_NO_SPACE;

    buffer[used] = '\0';
    rc = parse_buffer(buffer, config);
    if (rc == MINI_OK && out_loaded_from_file != NULL) *out_loaded_from_file = true;
    return rc;
}

/* The complete canonical file is bounded below 1024 bytes, including five
 * maximum-length memories and full-width GPIO identifiers. */
static void append(char *buffer, uint32_t *used, const char *text)
{
    while (*text) buffer[(*used)++] = *text++;
}
static void field(char *buffer, uint32_t *used, const char *key, const char *value)
{
    append(buffer, used, key); append(buffer, used, "=");
    append(buffer, used, value); append(buffer, used, "\n");
}
static void number_field(char *b, uint32_t *n, const char *key, uint32_t value)
{
    char text[11];
    keyer_text_number(text, value);
    field(b, n, key, text);
}

mini_result_t config_service_save(const mini_api_t *api, const keyer_config_t *c)
{
    static const char *const inputs[] = {"Paddle", "PaddleR", "SK-T", "SK-R"};
    static const char *const outputs[] = {"SKS", "SKM", "Off"};
    static const char *const paddles[] = {"IambicA", "IambicB", "Bug"};
    const char *temp = "/flash/keyer/setting.tmp";
    char b[KEYER_SETTING_BUFFER_SIZE];
    uint32_t n = 0, sent = 0;
    mini_file_t file;
    if (!api || !api->fs || !c) return MINI_ERR_INVALID;
    const mini_fs_api_t *fs = api->fs;
    if (!fs->open || !fs->write || !fs->sync || !fs->close || !fs->rename ||
        !fs->remove_file || !fs->mkdir) return MINI_ERR_NOT_READY;
    if (c->wpm < 5 || c->wpm > 60 || c->volume > 99 || c->repeat_s < 1 ||
        c->repeat_s > 99 || c->tx_delay_s > 99 || c->tune_timeout_s > 20 ||
        c->sidetone_hz < 300 || c->sidetone_hz > 999 ||
        (unsigned)c->key_in_mode > 3 || (unsigned)c->key_out_mode > 2 ||
        (unsigned)c->paddle_mode > 2) return MINI_ERR_INVALID;
    for (unsigned i = 0; i < 5; ++i) {
        unsigned j = 0;
        while (j < 96 && c->messages[i][j]) {
            if (c->messages[i][j] < 32 || c->messages[i][j] > 126) return MINI_ERR_INVALID;
            ++j;
        }
        if (j == 96) return MINI_ERR_INVALID;
    }
    number_field(b, &n, "wpm", c->wpm);
    number_field(b, &n, "volume", c->volume);
    field(b, &n, "sidetone", c->sidetone_enabled ? "On" : "Off");
    number_field(b, &n, "sidetone_hz", c->sidetone_hz);
    field(b, &n, "key_in", inputs[c->key_in_mode]);
    field(b, &n, "key_out", outputs[c->key_out_mode]);
    field(b, &n, "paddle", paddles[c->paddle_mode]);
    number_field(b, &n, "key_in_tip_gpio", c->key_in_tip_line);
    number_field(b, &n, "key_in_ring_gpio", c->key_in_ring_line);
    number_field(b, &n, "key_out_tip_gpio", c->key_out_tip_line);
    number_field(b, &n, "key_out_ring_gpio", c->key_out_ring_line);
    for (unsigned i = 0; i < 5; ++i) {
        char key[3] = {'m', (char)('1' + i), 0};
        field(b, &n, key, c->messages[i]);
    }
    number_field(b, &n, "repeat_s", c->repeat_s);
    number_field(b, &n, "tx_delay_s", c->tx_delay_s);
    number_field(b, &n, "tune_timeout_s", c->tune_timeout_s);
    field(b, &n, "mute", c->mute ? "On" : "Off");
    mini_result_t rc = fs->mkdir("/flash/keyer");
    if (rc != MINI_OK && rc != MINI_ERR_EXISTS) return rc;
    rc = fs->open(temp, MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC, &file);
    if (rc != MINI_OK) return rc;
    while (sent < n) {
        uint32_t wrote = 0;
        rc = fs->write(file, b + sent, n - sent, &wrote);
        if (rc != MINI_OK) break;
        if (!wrote || wrote > n - sent) { rc = MINI_ERR_IO; break; }
        sent += wrote;
    }
    if (rc == MINI_OK) rc = fs->sync(file);
    mini_result_t closed = fs->close(file);
    if (rc == MINI_OK) rc = closed;
    if (rc == MINI_OK) rc = fs->rename(temp, KEYER_SETTING_PATH);
    if (rc != MINI_OK) (void)fs->remove_file(temp);
    return rc;
}
