#include <cstdint>
#include <cstring>

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "adv_i2c.h"
#include "adv_internal.h"

namespace {
constexpr uint8_t kAddress = 0x34u;
constexpr uint32_t kI2cHz = 400000u;

constexpr uint8_t REG_CFG = 0x01u;
constexpr uint8_t REG_INT_STAT = 0x02u;
constexpr uint8_t REG_KEY_LCK_EC = 0x03u;
constexpr uint8_t REG_KEY_EVENT_A = 0x04u;
constexpr uint8_t REG_GPIO_INT_STAT_1 = 0x11u;
constexpr uint8_t REG_GPIO_INT_STAT_2 = 0x12u;
constexpr uint8_t REG_GPIO_INT_STAT_3 = 0x13u;
constexpr uint8_t REG_GPIO_INT_EN_1 = 0x1au;
constexpr uint8_t REG_GPIO_INT_EN_2 = 0x1bu;
constexpr uint8_t REG_GPIO_INT_EN_3 = 0x1cu;
constexpr uint8_t REG_KP_GPIO_1 = 0x1du;
constexpr uint8_t REG_KP_GPIO_2 = 0x1eu;
constexpr uint8_t REG_KP_GPIO_3 = 0x1fu;
constexpr uint8_t REG_GPI_EM_1 = 0x20u;
constexpr uint8_t REG_GPI_EM_2 = 0x21u;
constexpr uint8_t REG_GPI_EM_3 = 0x22u;
constexpr uint8_t REG_GPIO_DIR_1 = 0x23u;
constexpr uint8_t REG_GPIO_DIR_2 = 0x24u;
constexpr uint8_t REG_GPIO_DIR_3 = 0x25u;
constexpr uint8_t REG_GPIO_INT_LVL_1 = 0x26u;
constexpr uint8_t REG_GPIO_INT_LVL_2 = 0x27u;
constexpr uint8_t REG_GPIO_INT_LVL_3 = 0x28u;
constexpr uint8_t REG_DEBOUNCE_DIS_1 = 0x29u;
constexpr uint8_t REG_DEBOUNCE_DIS_2 = 0x2au;
constexpr uint8_t REG_DEBOUNCE_DIS_3 = 0x2bu;
constexpr uint8_t CFG_GPI_IEN = 0x02u;
constexpr uint8_t CFG_KE_IEN = 0x01u;

constexpr uint32_t kPhysicalRows = 4u;
constexpr uint32_t kPhysicalColumns = 14u;

i2c_master_dev_handle_t s_device = nullptr;
bool s_ready = false;
bool s_pressed[kPhysicalRows][kPhysicalColumns];

const char kFirst[kPhysicalRows][kPhysicalColumns] = {
    {'`','1','2','3','4','5','6','7','8','9','0','-','=',0},
    {0,'q','w','e','r','t','y','u','i','o','p','[',']','\\'},
    {0,0,'a','s','d','f','g','h','j','k','l',';','\'',0},
    {0,0,0,'z','x','c','v','b','n','m',',','.','/',' '},
};

const char kSecond[kPhysicalRows][kPhysicalColumns] = {
    {'~','!','@','#','$','%','^','&','*','(',')','_','+',0},
    {0,'Q','W','E','R','T','Y','U','I','O','P','{','}','|'},
    {0,0,'A','S','D','F','G','H','J','K','L',':','"',0},
    {0,0,0,'Z','X','C','V','B','N','M','<','>','?',' '},
};

bool read_reg(uint8_t reg, uint8_t *out_value)
{
    if (s_device == nullptr || out_value == nullptr) return false;
    return i2c_master_transmit_receive(s_device, &reg, 1u, out_value, 1u, 100) == ESP_OK;
}

bool write_reg(uint8_t reg, uint8_t value)
{
    if (s_device == nullptr) return false;
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(s_device, data, sizeof(data), 100) == ESP_OK;
}

void flush_raw(void)
{
    for (int guard = 0; guard < 16; ++guard) {
        uint8_t count = 0u;
        if (!read_reg(REG_KEY_LCK_EC, &count)) break;
        count &= 0x0fu;
        if (count == 0u) break;
        uint8_t ignored = 0u;
        if (!read_reg(REG_KEY_EVENT_A, &ignored)) break;
    }
    uint8_t ignored = 0u;
    (void)read_reg(REG_GPIO_INT_STAT_1, &ignored);
    (void)read_reg(REG_GPIO_INT_STAT_2, &ignored);
    (void)read_reg(REG_GPIO_INT_STAT_3, &ignored);
    (void)write_reg(REG_INT_STAT, 0x03u);
}

uint32_t modifiers(void)
{
    uint32_t mods = 0u;
    if (s_pressed[2][1]) mods |= MINI_MOD_SHIFT;
    if (s_pressed[3][0]) mods |= MINI_MOD_CTRL;
    if (s_pressed[3][2]) mods |= MINI_MOD_ALT;
    if (s_pressed[2][0]) mods |= MINI_MOD_FN;
    if (s_pressed[3][1]) mods |= MINI_MOD_OPT;
    return mods;
}

bool modifier_key(uint32_t row, uint32_t column, uint32_t *out_key)
{
    if (row == 2u && column == 0u) { *out_key = MINI_KEY_FN; return true; }
    if (row == 2u && column == 1u) { *out_key = MINI_KEY_SHIFT; return true; }
    if (row == 3u && column == 0u) { *out_key = MINI_KEY_CTRL; return true; }
    if (row == 3u && column == 1u) { *out_key = MINI_KEY_OPT; return true; }
    if (row == 3u && column == 2u) { *out_key = MINI_KEY_ALT; return true; }
    return false;
}

void make_special(mini_key_event_t *out_event, uint32_t key, uint32_t mods)
{
    std::memset(out_event, 0, sizeof(*out_event));
    out_event->struct_size = sizeof(*out_event);
    out_event->type = MINI_KEY_EVENT_SPECIAL;
    out_event->key = key;
    out_event->modifiers = mods;
}

void make_char(mini_key_event_t *out_event, uint8_t ch, uint32_t mods)
{
    std::memset(out_event, 0, sizeof(*out_event));
    out_event->struct_size = sizeof(*out_event);
    out_event->type = MINI_KEY_EVENT_CHAR;
    out_event->codepoint = ch;
    out_event->modifiers = mods;
}

bool fn_special(char base, uint32_t *out_key)
{
    switch (base) {
        case ';': *out_key = MINI_KEY_UP; return true;
        case ',': *out_key = MINI_KEY_LEFT; return true;
        case '.': *out_key = MINI_KEY_DOWN; return true;
        case '/': *out_key = MINI_KEY_RIGHT; return true;
        case '`': *out_key = MINI_KEY_ESCAPE; return true;
        default: return false;
    }
}

bool process_raw(uint8_t raw, mini_key_event_t *out_event)
{
    const bool pressed = (raw & 0x80u) != 0u;
    const uint8_t key_number = raw & 0x7fu;
    if (key_number == 0u || key_number > 80u) return false;

    const uint8_t index = static_cast<uint8_t>(key_number - 1u);
    const uint32_t raw_row = index / 10u;
    const uint32_t raw_column = index % 10u;
    if (raw_row >= 7u || raw_column >= 8u) return false;

    const uint32_t column = raw_row * 2u + (raw_column > 3u ? 1u : 0u);
    const uint32_t row = raw_column % 4u;
    if (row >= kPhysicalRows || column >= kPhysicalColumns) return false;

    s_pressed[row][column] = pressed;
    const uint32_t mods = modifiers();
    if (!pressed) return false;

    uint32_t modifier = 0u;
    if (modifier_key(row, column, &modifier)) {
        make_special(out_event, modifier, mods);
        return true;
    }

    if (row == 0u && column == 13u) {
        make_special(out_event, (mods & MINI_MOD_FN) != 0u ? MINI_KEY_DELETE : MINI_KEY_BACKSPACE, mods);
        return true;
    }
    if (row == 1u && column == 0u) {
        make_special(out_event, MINI_KEY_TAB, mods);
        return true;
    }
    if (row == 2u && column == 13u) {
        make_special(out_event, MINI_KEY_ENTER, mods);
        return true;
    }

    const char base = kFirst[row][column];
    if (base == 0) return false;

    if ((mods & MINI_MOD_FN) != 0u) {
        uint32_t special = 0u;
        if (fn_special(base, &special)) {
            make_special(out_event, special, mods);
            return true;
        }
    }

    const char ch = (mods & MINI_MOD_SHIFT) != 0u ? kSecond[row][column] : base;
    if (ch == 0) return false;
    make_char(out_event, static_cast<uint8_t>(ch), mods);
    return true;
}
}

extern "C" int adv_keyboard_prepare(void)
{
    if (s_ready) return 0;
    if (adv_i2c_prepare() != 0) return -1;

    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = kAddress;
    config.scl_speed_hz = kI2cHz;

    if (i2c_master_bus_add_device(adv_i2c_bus(), &config, &s_device) != ESP_OK) {
        s_device = nullptr;
        return -1;
    }

    const struct { uint8_t reg; uint8_t value; } setup[] = {
        {REG_GPIO_DIR_1, 0x00u}, {REG_GPIO_DIR_2, 0x00u}, {REG_GPIO_DIR_3, 0x00u},
        {REG_GPI_EM_1, 0xffu}, {REG_GPI_EM_2, 0xffu}, {REG_GPI_EM_3, 0xffu},
        {REG_GPIO_INT_LVL_1, 0x00u}, {REG_GPIO_INT_LVL_2, 0x00u}, {REG_GPIO_INT_LVL_3, 0x00u},
        {REG_GPIO_INT_EN_1, 0xffu}, {REG_GPIO_INT_EN_2, 0xffu}, {REG_GPIO_INT_EN_3, 0xffu},
        {REG_KP_GPIO_1, 0x7fu}, {REG_KP_GPIO_2, 0xffu}, {REG_KP_GPIO_3, 0x00u},
        {REG_DEBOUNCE_DIS_1, 0x00u}, {REG_DEBOUNCE_DIS_2, 0x00u}, {REG_DEBOUNCE_DIS_3, 0x00u},
    };
    for (const auto &item : setup) {
        if (!write_reg(item.reg, item.value)) return -1;
    }

    flush_raw();
    uint8_t cfg = 0u;
    if (!read_reg(REG_CFG, &cfg)) return -1;
    if (!write_reg(REG_CFG, static_cast<uint8_t>(cfg | CFG_GPI_IEN | CFG_KE_IEN))) return -1;

    std::memset(s_pressed, 0, sizeof(s_pressed));
    s_ready = true;
    return 0;
}

extern "C" bool adv_keyboard_ready(void)
{
    return s_ready;
}

extern "C" mini_result_t adv_keyboard_read_event(mini_key_event_t *out_event)
{
    if (!s_ready) return MINI_ERR_NOT_READY;
    if (out_event == nullptr) return MINI_ERR_INVALID;

    for (;;) {
        uint8_t count = 0u;
        if (!read_reg(REG_KEY_LCK_EC, &count)) return MINI_ERR_IO;
        count &= 0x0fu;
        if (count == 0u) return MINI_ERR_NOT_READY;

        uint8_t raw = 0u;
        if (!read_reg(REG_KEY_EVENT_A, &raw)) return MINI_ERR_IO;
        if (process_raw(raw, out_event)) return MINI_OK;
    }
}

extern "C" void adv_keyboard_flush(void)
{
    if (!s_ready) return;
    flush_raw();
    std::memset(s_pressed, 0, sizeof(s_pressed));
}
