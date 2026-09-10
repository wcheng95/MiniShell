#include "ui_shell.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *screen_name(Screen screen)
{
    switch (screen) {
        case SCREEN_RX: return "RX";
        case SCREEN_TX: return "TX";
        case SCREEN_O: return "O";
        case SCREEN_S: return "S";
        case SCREEN_V: return "V";
        default: return "?";
    }
}

static void frame_set(UiFrame *frame, int row, const char *fmt, ...)
{
    if (frame == NULL || row < 0 || (uint32_t)row >= frame->row_count ||
        frame->column_count == 0u || frame->column_count > UI_MAX_COLS) {
        return;
    }

    char temp[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(temp, sizeof(temp), fmt, ap);
    va_end(ap);

    size_t len = strlen(temp);
    if (len > frame->column_count) len = frame->column_count;
    memcpy(frame->rows[row], temp, len);
    for (size_t i = len; i < frame->column_count; ++i) frame->rows[row][i] = ' ';
    frame->rows[row][frame->column_count] = '\0';
}

static void frame_footer(UiFrame *frame, const char *fmt, ...)
{
    if (frame == NULL || !frame->has_footer || frame->row_count == 0u) return;

    char text[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    frame_set(frame, (int)(frame->row_count - 1u), "%s", text);
}

static void row_item(const UiShell *ui, UiFrame *frame, int line, const char *fmt, ...)
{
    char text[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);

    frame_set(frame, line + 1, "%c%d %s",
              ui->selected_line == line ? '>' : ' ', line + 1, text);
}

static void info_line(UiFrame *frame, int line, const char *fmt, ...)
{
    char text[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    frame_set(frame, line + 1, "%s", text);
}

static void format_memory_bytes(uint64_t bytes, char *out, size_t out_size)
{
    const uint64_t kib = 1024ull;
    const uint64_t mib = 1024ull * 1024ull;
    const uint64_t gib = 1024ull * 1024ull * 1024ull;
    uint64_t unit = 1u;
    char suffix = 'B';

    if (bytes >= gib) { unit = gib; suffix = 'G'; }
    else if (bytes >= mib) { unit = mib; suffix = 'M'; }
    else if (bytes >= kib) { unit = kib; suffix = 'K'; }

    if (unit == 1u) {
        (void)snprintf(out, out_size, "%lluB", (unsigned long long)bytes);
    } else {
        uint64_t whole = bytes / unit;
        uint64_t tenth = ((bytes % unit) * 10u) / unit;
        (void)snprintf(out, out_size, "%llu.%llu%c",
                       (unsigned long long)whole,
                       (unsigned long long)tenth, suffix);
    }
}

static uint32_t paged_count(size_t item_count)
{
    uint32_t pages = (uint32_t)((item_count + UI_MAIN_LINES - 1u) / UI_MAIN_LINES);
    return pages == 0u ? 1u : pages;
}

static uint32_t screen_page_count(const UiShell *ui, const UiModel *model)
{
    if (ui->submenu != UI_SUBMENU_NONE) return 1u;
    if (ui->screen == SCREEN_RX) return paged_count(model->rx_count);
    if (ui->screen == SCREEN_TX) return paged_count(model->tx_count);
    return 1u;
}

static uint32_t visible_page(const UiShell *ui, const UiModel *model)
{
    uint32_t pages = screen_page_count(ui, model);
    return pages == 0u ? 0u : ui->page_index % pages;
}

static unsigned band_number(const char *name)
{
    unsigned value = 0u;
    int found = 0;
    if (name == NULL) return 0u;
    while (*name >= '0' && *name <= '9') {
        found = 1;
        value = value * 10u + (unsigned)(*name - '0');
        ++name;
    }
    return found ? value : 0u;
}

static char counter_char(uint8_t counter)
{
    counter %= 15u;
    return counter < 10u ? (char)('0' + counter)
                         : (char)('A' + (counter - 10u));
}

static void render_top(const UiShell *ui, const UiModel *model, UiFrame *frame)
{
    if (ui->presentation == FT8_PRESENTATION_ADV) {
        char utc[9];
        uint32_t pages = screen_page_count(ui, model);
        uint32_t page = visible_page(ui, model) + 1u;
        unsigned band = band_number(model->band_name) % 100u;

        if (model->utc_valid) {
            unsigned hour = (unsigned)model->utc_hour % 24u;
            unsigned minute = (unsigned)model->utc_minute % 60u;
            unsigned second = (unsigned)model->utc_second % 60u;
            utc[0] = (char)('0' + hour / 10u);
            utc[1] = (char)('0' + hour % 10u);
            utc[2] = ':';
            utc[3] = (char)('0' + minute / 10u);
            utc[4] = (char)('0' + minute % 10u);
            utc[5] = ':';
            utc[6] = (char)('0' + second / 10u);
            utc[7] = (char)('0' + second % 10u);
            utc[8] = '\0';
        } else {
            memcpy(utc, "--:--:--", sizeof(utc));
        }

        /* Locked ADV first-release format: exactly 20 characters. */
        frame_set(frame, 0, "%-2.2s %02u %s %u/%u %c",
                  screen_name(ui->screen), band, utc,
                  (unsigned)page, (unsigned)pages,
                  counter_char(model->slot_counter));
        return;
    }

    frame_set(frame, 0, "%-4s %-4s %-10s %-2s",
              "FT8", model->band_name, model->profile_name, screen_name(ui->screen));
}

static void render_rx(const UiShell *ui, const UiModel *model, UiFrame *frame)
{
    size_t start = (size_t)visible_page(ui, model) * UI_MAIN_LINES;
    for (int i = 0; i < UI_MAIN_LINES; ++i) {
        size_t index = start + (size_t)i;
        if (index < model->rx_count) {
            frame_set(frame, i + 1, "%d %s", i + 1, model->rx_lines[index]);
        }
    }
    frame_footer(frame, "R T O S V  1-6 select q quit");
}

static void render_tx(const UiShell *ui, const UiModel *model, UiFrame *frame)
{
    size_t start = (size_t)visible_page(ui, model) * UI_MAIN_LINES;
    for (int i = 0; i < UI_MAIN_LINES; ++i) {
        size_t index = start + (size_t)i;
        if (index < model->tx_count) {
            frame_set(frame, i + 1, "%d %s", i + 1, model->tx_lines[index]);
        }
    }
    frame_footer(frame, "1-6 drop  Enter rotate  Up/Down page");
}

static void render_o_root(const UiShell *ui, const UiModel *model, UiFrame *frame)
{
    row_item(ui, frame, 0, "Protocol: FT8");
    row_item(ui, frame, 1, "Profile: %s", model->profile_name);
    row_item(ui, frame, 2, "Band: %s", model->band_name);
    row_item(ui, frame, 3, "CQ / Beacon >");
    row_item(ui, frame, 4, "TX >");
    row_item(ui, frame, 5, "Message >");
    frame_footer(frame, "2-6 Enter <>chg `back q quit");
}

static void render_o_cq(const UiShell *ui, UiFrame *frame)
{
    row_item(ui, frame, 0, "CQ Type: --");
    row_item(ui, frame, 1, "Beacon: --");
    frame_footer(frame, "CQ controls later  `back q quit");
}

static void render_o_tx(const UiShell *ui, const UiModel *model, UiFrame *frame)
{
    row_item(ui, frame, 0, "Offset Source: --");
    row_item(ui, frame, 1, "Fixed Offset: --");
    row_item(ui, frame, 2, "Skip TX1: %s", model->skip_tx1 ? "ON" : "OFF");
    row_item(ui, frame, 3, "Max Retry: %d", model->max_retry);
    row_item(ui, frame, 4, "Tune: --");
    frame_footer(frame, "<> changes wired items `back q");
}

static void render_o_message(const UiShell *ui, UiFrame *frame)
{
    row_item(ui, frame, 0, "Send FreeText >");
    row_item(ui, frame, 1, "Edit FreeText >");
    row_item(ui, frame, 2, "Current: (empty)");
    frame_footer(frame, "message editor later `back q quit");
}

static void render_o(const UiShell *ui, const UiModel *model, UiFrame *frame)
{
    switch (ui->submenu) {
        case UI_SUBMENU_O_CQ: render_o_cq(ui, frame); break;
        case UI_SUBMENU_O_TX: render_o_tx(ui, model, frame); break;
        case UI_SUBMENU_O_MESSAGE: render_o_message(ui, frame); break;
        default: render_o_root(ui, model, frame); break;
    }
}

static void render_s_root(const UiShell *ui, UiFrame *frame)
{
    row_item(ui, frame, 0, "Station >");
    row_item(ui, frame, 1, "I/O Paths >");
    row_item(ui, frame, 2, "Band Profiles >");
    row_item(ui, frame, 3, "Logging >");
    row_item(ui, frame, 4, "Time / GPS >");
    row_item(ui, frame, 5, "System >");
    frame_footer(frame, "1-6 Enter        `back q quit");
}

static void render_s_station(const UiShell *ui, UiFrame *frame)
{
    row_item(ui, frame, 0, "Callsign: --");
    row_item(ui, frame, 1, "Grid: --");
    row_item(ui, frame, 2, "Ignore List: --");
    row_item(ui, frame, 3, "ADIF Comment: --");
    frame_footer(frame, "text editors later `back q quit");
}

static void render_s_io_paths(const UiShell *ui, UiFrame *frame)
{
    row_item(ui, frame, 0, "RX Audio: --");
    row_item(ui, frame, 1, "TX Audio: --");
    row_item(ui, frame, 2, "Control: --");
    frame_footer(frame, "independent paths `back q quit");
}

static void render_s_band_profiles(const UiShell *ui, const UiModel *model, UiFrame *frame)
{
    row_item(ui, frame, 0, "Edit: %s", model->profile_name);
    row_item(ui, frame, 1, "Enabled Bands >");
    row_item(ui, frame, 2, "Frequencies >");
    row_item(ui, frame, 3, "User Profiles >");
    frame_footer(frame, "profile editor later `back q quit");
}

static void render_s_logging(const UiShell *ui, UiFrame *frame)
{
    row_item(ui, frame, 0, "RxTx Log: --");
    row_item(ui, frame, 1, "ADIF Log: --");
    row_item(ui, frame, 2, "ADIF Comment: --");
    frame_footer(frame, "logging later       `back q");
}

static void render_s_time_gps(const UiShell *ui, UiFrame *frame)
{
    row_item(ui, frame, 0, "GNSS Source: --");
    row_item(ui, frame, 1, "GNSS LoRa: --");
    row_item(ui, frame, 2, "Date: --");
    row_item(ui, frame, 3, "Time: --");
    row_item(ui, frame, 4, "Sync Now: --");
    frame_footer(frame, "time/GPS later    `back q quit");
}

static void render_s_system(const UiShell *ui, UiFrame *frame)
{
    row_item(ui, frame, 0, "Copy Files to SD");
    row_item(ui, frame, 1, "Delete Files >");
    row_item(ui, frame, 2, "Sleep");
    row_item(ui, frame, 3, "Restart");
    row_item(ui, frame, 4, "Storage: --");
    row_item(ui, frame, 5, "Config: station.txt");
    frame_footer(frame, "system actions later `back q");
}

static void render_s(const UiShell *ui, const UiModel *model, UiFrame *frame)
{
    switch (ui->submenu) {
        case UI_SUBMENU_S_STATION: render_s_station(ui, frame); break;
        case UI_SUBMENU_S_IO_PATHS: render_s_io_paths(ui, frame); break;
        case UI_SUBMENU_S_BAND_PROFILES: render_s_band_profiles(ui, model, frame); break;
        case UI_SUBMENU_S_LOGGING: render_s_logging(ui, frame); break;
        case UI_SUBMENU_S_TIME_GPS: render_s_time_gps(ui, frame); break;
        case UI_SUBMENU_S_SYSTEM: render_s_system(ui, frame); break;
        default: render_s_root(ui, model, frame); break;
    }
}

static void render_v_root(const UiShell *ui, UiFrame *frame)
{
    row_item(ui, frame, 0, "Memory >");
    row_item(ui, frame, 1, "GPS >");
    row_item(ui, frame, 2, "QSO / Log >");
    row_item(ui, frame, 3, "Performance >");
    row_item(ui, frame, 4, "System Info >");
    row_item(ui, frame, 5, "About >");
    frame_footer(frame, "1-6 Enter  read only  q quit");
}

static void render_v_memory(const UiModel *model, UiFrame *frame)
{
    char free_text[24] = "--";
    char largest_text[24] = "--";
    char app_text[24] = "--";

    if (model->memory_free_valid) {
        format_memory_bytes(model->memory_free_bytes, free_text, sizeof(free_text));
    }
    if (model->memory_largest_valid) {
        format_memory_bytes(model->memory_largest_free_block,
                            largest_text, sizeof(largest_text));
    }
    if (model->memory_app_valid) {
        format_memory_bytes(model->memory_app_allocated_bytes, app_text, sizeof(app_text));
    }

    info_line(frame, 0, "Heap free: %s", free_text);
    info_line(frame, 1, "Largest: %s", largest_text);
    info_line(frame, 2, "App alloc: %s", app_text);
    if (model->memory_app_valid) {
        info_line(frame, 3, "Alloc count: %u",
                  (unsigned)model->memory_app_allocation_count);
    } else {
        info_line(frame, 3, "Alloc count: --");
    }

    /* This is contiguous-largest/free, not a fragmentation percentage. */
    if (model->memory_free_valid && model->memory_largest_valid &&
        model->memory_free_bytes > 0u) {
        uint64_t largest = model->memory_largest_free_block;
        uint64_t free_bytes = model->memory_free_bytes;
        uint64_t percent = largest >= free_bytes
                               ? 100u
                               : (largest * 100u) / free_bytes;
        info_line(frame, 4, "Largest/free: %u%%", (unsigned)percent);
    } else {
        info_line(frame, 4, "Largest/free: --");
    }
    info_line(frame, 5, "RX: %s", model->rx_active ? "ON" : "OFF");
    frame_footer(frame, "read only        `back q quit");
}

static void render_v_gps(UiFrame *frame)
{
    info_line(frame, 0, "Fix: --");
    info_line(frame, 1, "UTC: --");
    info_line(frame, 2, "Grid: --");
    info_line(frame, 3, "Source: --");
    info_line(frame, 4, "Satellites: --");
    frame_footer(frame, "read only        `back q quit");
}

static void render_v_qso(UiFrame *frame)
{
    info_line(frame, 0, "QSOs: 0 (prototype)");
    info_line(frame, 1, "Last QSO: --");
    info_line(frame, 2, "ADIF: --");
    info_line(frame, 3, "RxTx Log: --");
    frame_footer(frame, "read only        `back q quit");
}

static void render_v_perf(UiFrame *frame)
{
    info_line(frame, 0, "Decode time: --");
    info_line(frame, 1, "Candidates: --");
    info_line(frame, 2, "CPU: --");
    info_line(frame, 3, "Memory: --");
    info_line(frame, 4, "Audio blocks: --");
    frame_footer(frame, "read only        `back q quit");
}

static void render_v_system(const UiShell *ui, const UiModel *model, UiFrame *frame)
{
    ft8_presentation_spec_t spec;
    (void)ft8_presentation_get_spec(ui->presentation, &spec);

    info_line(frame, 0, "Runtime: MiniShell");
    info_line(frame, 1, "Presentation: %s", ft8_presentation_name(ui->presentation));
    info_line(frame, 2, "UI: text %ux%u", (unsigned)spec.columns, (unsigned)spec.rows);
    info_line(frame, 3, "App: ft8");
    info_line(frame, 4, "Station: %s", model->profile_name);
    info_line(frame, 5, "Band: %s", model->band_name);
    frame_footer(frame, "read only        `back q quit");
}

static void render_v_about(UiFrame *frame)
{
    info_line(frame, 0, "MiniFT8-V3");
    info_line(frame, 1, "MiniShell application");
    info_line(frame, 2, "Runtime app: ft8");
    info_line(frame, 3, "FT8 protocol only");
    info_line(frame, 4, "V is strictly read-only");
    frame_footer(frame, "read only        `back q quit");
}

static void render_v(const UiShell *ui, const UiModel *model, UiFrame *frame)
{
    switch (ui->submenu) {
        case UI_SUBMENU_V_MEMORY: render_v_memory(model, frame); break;
        case UI_SUBMENU_V_GPS: render_v_gps(frame); break;
        case UI_SUBMENU_V_QSO: render_v_qso(frame); break;
        case UI_SUBMENU_V_PERF: render_v_perf(frame); break;
        case UI_SUBMENU_V_SYSTEM: render_v_system(ui, model, frame); break;
        case UI_SUBMENU_V_ABOUT: render_v_about(frame); break;
        default: render_v_root(ui, frame); break;
    }
}

void ui_shell_init(UiShell *ui, ft8_presentation_profile_t presentation)
{
    ui->screen = SCREEN_RX;
    ui->submenu = UI_SUBMENU_NONE;
    ui->selected_line = 0;
    ui->page_index = 0u;
    ui->presentation = presentation;
}

void ui_shell_render(const UiShell *ui, const UiModel *model, UiFrame *frame)
{
    ft8_presentation_spec_t spec;
    if (!ft8_presentation_get_spec(ui->presentation, &spec)) {
        (void)ft8_presentation_get_spec(FT8_PRESENTATION_DESKTOP, &spec);
    }

    memset(frame, 0, sizeof(*frame));
    frame->column_count = spec.columns;
    frame->row_count = spec.rows;
    frame->has_footer = spec.has_footer;
    for (uint32_t r = 0u; r < frame->row_count; ++r) frame_set(frame, (int)r, "");

    render_top(ui, model, frame);
    switch (ui->screen) {
        case SCREEN_RX: render_rx(ui, model, frame); break;
        case SCREEN_TX: render_tx(ui, model, frame); break;
        case SCREEN_O: render_o(ui, model, frame); break;
        case SCREEN_S: render_s(ui, model, frame); break;
        case SCREEN_V: render_v(ui, model, frame); break;
    }
}

static void enter_screen(UiShell *ui, Screen screen)
{
    ui->screen = screen;
    ui->submenu = UI_SUBMENU_NONE;
    ui->selected_line = 0;
    ui->page_index = 0u;
}

static void clear_action(AppAction *action)
{
    action->type = APP_ACTION_NONE;
}

static bool emit_set_profile(const UiModel *model, int delta, AppAction *action)
{
    if (model->profile_count <= 0) return false;
    int next = (model->profile_index + delta + model->profile_count) % model->profile_count;
    action->type = APP_ACTION_SET_PROFILE;
    action->value.index = next;
    return true;
}

static bool emit_set_band(const UiModel *model, int delta, AppAction *action)
{
    if (model->band_count <= 0) return false;
    int next = (model->band_index + delta + model->band_count) % model->band_count;
    action->type = APP_ACTION_SET_BAND;
    action->value.index = next;
    return true;
}

static bool activate_line(UiShell *ui, const UiModel *model, int line, AppAction *action)
{
    size_t item_index;

    if (line < 0 || line >= UI_MAIN_LINES) return false;
    ui->selected_line = line;

    if (ui->screen == SCREEN_RX && ui->submenu == UI_SUBMENU_NONE) {
        item_index = (size_t)visible_page(ui, model) * UI_MAIN_LINES + (size_t)line;
        if (item_index >= model->rx_count) return false;
        action->type = APP_ACTION_SELECT_RX_MESSAGE;
        action->value.index = (int)item_index;
        return true;
    }

    if (ui->screen == SCREEN_TX && ui->submenu == UI_SUBMENU_NONE) {
        item_index = (size_t)visible_page(ui, model) * UI_MAIN_LINES + (size_t)line;
        if (item_index >= model->tx_count) return false;
        action->type = APP_ACTION_DROP_TX_QSO;
        action->value.index = (int)item_index;
        return true;
    }

    if (ui->screen == SCREEN_O && ui->submenu == UI_SUBMENU_NONE) {
        if (line == 0) return false;
        if (line == 1) return emit_set_profile(model, +1, action);
        if (line == 2) return emit_set_band(model, +1, action);
        if (line == 3) { ui->submenu = UI_SUBMENU_O_CQ; ui->selected_line = 0; return false; }
        if (line == 4) { ui->submenu = UI_SUBMENU_O_TX; ui->selected_line = 0; return false; }
        if (line == 5) { ui->submenu = UI_SUBMENU_O_MESSAGE; ui->selected_line = 0; return false; }
    }

    if (ui->screen == SCREEN_O && ui->submenu == UI_SUBMENU_O_TX) {
        if (line == 2) {
            action->type = APP_ACTION_SET_SKIP_TX1;
            action->value.bool_value = !model->skip_tx1;
            return true;
        }
        if (line == 3) {
            action->type = APP_ACTION_SET_MAX_RETRY;
            action->value.int_value = model->max_retry + 1;
            return true;
        }
    }

    if (ui->screen == SCREEN_S && ui->submenu == UI_SUBMENU_NONE) {
        static const UiSubmenu items[UI_MAIN_LINES] = {
            UI_SUBMENU_S_STATION, UI_SUBMENU_S_IO_PATHS, UI_SUBMENU_S_BAND_PROFILES,
            UI_SUBMENU_S_LOGGING, UI_SUBMENU_S_TIME_GPS, UI_SUBMENU_S_SYSTEM
        };
        ui->submenu = items[line];
        ui->selected_line = 0;
        ui->page_index = 0u;
        return false;
    }

    if (ui->screen == SCREEN_V && ui->submenu == UI_SUBMENU_NONE) {
        static const UiSubmenu items[UI_MAIN_LINES] = {
            UI_SUBMENU_V_MEMORY, UI_SUBMENU_V_GPS, UI_SUBMENU_V_QSO,
            UI_SUBMENU_V_PERF, UI_SUBMENU_V_SYSTEM, UI_SUBMENU_V_ABOUT
        };
        ui->submenu = items[line];
        ui->selected_line = 0;
        ui->page_index = 0u;
        return false;
    }

    return false;
}

static bool adjust_selected(UiShell *ui, const UiModel *model, int delta, AppAction *action)
{
    if (ui->screen != SCREEN_O) return false;

    if (ui->submenu == UI_SUBMENU_NONE) {
        if (ui->selected_line == 1) return emit_set_profile(model, delta, action);
        if (ui->selected_line == 2) return emit_set_band(model, delta, action);
    } else if (ui->submenu == UI_SUBMENU_O_TX) {
        if (ui->selected_line == 2) {
            action->type = APP_ACTION_SET_SKIP_TX1;
            action->value.bool_value = !model->skip_tx1;
            return true;
        }
        if (ui->selected_line == 3) {
            int next = model->max_retry + delta;
            if (next < 0) next = 0;
            action->type = APP_ACTION_SET_MAX_RETRY;
            action->value.int_value = next;
            return true;
        }
    }

    return false;
}

static void move_page(UiShell *ui, const UiModel *model, int delta)
{
    uint32_t pages = screen_page_count(ui, model);
    if (pages <= 1u) {
        ui->page_index = 0u;
        return;
    }

    uint32_t current = visible_page(ui, model);
    if (delta < 0) {
        ui->page_index = current == 0u ? pages - 1u : current - 1u;
    } else {
        ui->page_index = (current + 1u) % pages;
    }
    ui->selected_line = 0;
}

bool ui_shell_handle_input(UiShell *ui, const UiModel *model,
                           UiInput input, AppAction *action_out)
{
    clear_action(action_out);

    if (input.type == UI_INPUT_CHAR) {
        int ch = tolower((unsigned char)input.ch);
        if (ch == 'r') { enter_screen(ui, SCREEN_RX); return false; }
        if (ch == 't') { enter_screen(ui, SCREEN_TX); return false; }
        if (ch == 'o') { enter_screen(ui, SCREEN_O); return false; }
        if (ch == 's') { enter_screen(ui, SCREEN_S); return false; }
        if (ch == 'v') { enter_screen(ui, SCREEN_V); return false; }
        if (ch >= '1' && ch <= '6') return activate_line(ui, model, ch - '1', action_out);
    }

    switch (input.type) {
        case UI_INPUT_UP:
        case UI_INPUT_PAGE_PREV:
            if (ui->submenu == UI_SUBMENU_NONE) {
                move_page(ui, model, -1);
            } else {
                ui->selected_line = (ui->selected_line + UI_MAIN_LINES - 1) % UI_MAIN_LINES;
            }
            return false;
        case UI_INPUT_DOWN:
        case UI_INPUT_PAGE_NEXT:
            if (ui->submenu == UI_SUBMENU_NONE) {
                move_page(ui, model, +1);
            } else {
                ui->selected_line = (ui->selected_line + 1) % UI_MAIN_LINES;
            }
            return false;
        case UI_INPUT_LEFT:
            return adjust_selected(ui, model, -1, action_out);
        case UI_INPUT_RIGHT:
            return adjust_selected(ui, model, +1, action_out);
        case UI_INPUT_ENTER:
            if (ui->screen == SCREEN_TX && ui->submenu == UI_SUBMENU_NONE) {
                action_out->type = APP_ACTION_ROTATE_TX_QUEUE;
                return true;
            }
            return activate_line(ui, model, ui->selected_line, action_out);
        case UI_INPUT_BACK:
            if (ui->submenu != UI_SUBMENU_NONE) {
                ui->submenu = UI_SUBMENU_NONE;
                ui->selected_line = 0;
                ui->page_index = 0u;
            } else if (ui->screen != SCREEN_RX) {
                enter_screen(ui, SCREEN_RX);
            }
            return false;
        default:
            return false;
    }
}
