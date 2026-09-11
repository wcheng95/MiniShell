#include "adv_rtc.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "adv_i2c.h"

#define ADV_RTC_I2C_HZ 400000u

#define DS3231_ADDRESS 0x68u
#define DS3231_REG_SECONDS 0x00u
#define DS3231_REG_STATUS 0x0fu
#define DS3231_STATUS_OSF 0x80u
#define DS3231_MONTH_CENTURY 0x80u
#define DS3231_HOUR_12H 0x40u
#define DS3231_HOUR_PM 0x20u

#define HYM8563_ADDRESS 0x51u
#define HYM8563_REG_CONTROL1 0x00u
#define HYM8563_CONTROL1_STOP 0x20u
#define HYM8563_REG_SECONDS 0x02u
#define HYM8563_SECONDS_VL 0x80u

/* M5 Unit RTC has shipped with HYM8563/BM8563-compatible parts. */
typedef enum {
    ADV_RTC_NONE = 0,
    ADV_RTC_DS3231,
    ADV_RTC_HYM8563,
} adv_rtc_kind_t;

static i2c_master_dev_handle_t s_device;
static adv_rtc_kind_t s_kind;

static int is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int days_in_month(int year, int month)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && is_leap(year)) return 29;
    return days[month - 1];
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

static void civil_from_days(int64_t z, int *year, unsigned *month, unsigned *day)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int y = (int)yoe + (int)(era * 400);
    unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    unsigned mp = (5u * doy + 2u) / 153u;
    unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
    unsigned m = mp < 10u ? mp + 3u : mp - 9u;
    y += m <= 2u;
    *year = y;
    *month = m;
    *day = d;
}

static int bcd_decode(uint8_t value)
{
    int hi = (value >> 4) & 0x0f;
    int lo = value & 0x0f;
    if (hi > 9 || lo > 9) return -1;
    return hi * 10 + lo;
}

static uint8_t bcd_encode(unsigned value)
{
    return (uint8_t)(((value / 10u) << 4) | (value % 10u));
}

static int ds3231_hour_decode(uint8_t value)
{
    if ((value & DS3231_HOUR_12H) == 0u) return bcd_decode(value & 0x3fu);

    int hour = bcd_decode(value & 0x1fu);
    if (hour < 1 || hour > 12) return -1;
    if (hour == 12) hour = 0;
    if ((value & DS3231_HOUR_PM) != 0u) hour += 12;
    return hour;
}

static bool read_block(uint8_t reg, uint8_t *data, size_t size)
{
    if (s_device == NULL || data == NULL || size == 0u) return false;
    return i2c_master_transmit_receive(s_device, &reg, 1u, data, size, 100) == ESP_OK;
}

static bool write_block(uint8_t reg, const uint8_t *data, size_t size)
{
    if (s_device == NULL || data == NULL || size == 0u || size > 8u) return false;
    uint8_t buffer[9];
    buffer[0] = reg;
    for (size_t i = 0; i < size; ++i) buffer[i + 1u] = data[i];
    return i2c_master_transmit(s_device, buffer, size + 1u, 100) == ESP_OK;
}

static bool write_byte(uint8_t reg, uint8_t value)
{
    return write_block(reg, &value, 1u);
}

static bool attach_and_probe(uint8_t address, uint8_t probe_reg, adv_rtc_kind_t kind)
{
    i2c_device_config_t config = {0};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = ADV_RTC_I2C_HZ;

    if (i2c_master_bus_add_device(adv_i2c_bus(), &config, &s_device) != ESP_OK) {
        s_device = NULL;
        return false;
    }

    uint8_t probe = 0u;
    if (!read_block(probe_reg, &probe, 1u)) {
        (void)i2c_master_bus_rm_device(s_device);
        s_device = NULL;
        return false;
    }

    s_kind = kind;
    return true;
}

static mini_result_t calendar_to_utc(int year, int month, int day,
                                     int hour, int minute, int second,
                                     int64_t *out_seconds,
                                     uint32_t *out_nanoseconds)
{
    if (second < 0 || second > 59 || minute < 0 || minute > 59 ||
        hour < 0 || hour > 23 || year < 2000 || year > 2099 ||
        month < 1 || month > 12 || day < 1 || day > days_in_month(year, month)) {
        return MINI_ERR_IO;
    }

    int64_t days = days_from_civil(year, (unsigned)month, (unsigned)day);
    *out_seconds = days * 86400 + hour * 3600 + minute * 60 + second;
    *out_nanoseconds = 0u;
    return MINI_OK;
}

static mini_result_t ds3231_load_utc(int64_t *out_seconds, uint32_t *out_nanoseconds)
{
    uint8_t status = 0u;
    if (!read_block(DS3231_REG_STATUS, &status, 1u)) return MINI_ERR_IO;
    if ((status & DS3231_STATUS_OSF) != 0u) return MINI_ERR_NOT_READY;

    uint8_t regs[7];
    if (!read_block(DS3231_REG_SECONDS, regs, sizeof(regs))) return MINI_ERR_IO;

    int second = bcd_decode(regs[0] & 0x7fu);
    int minute = bcd_decode(regs[1] & 0x7fu);
    int hour = ds3231_hour_decode(regs[2]);
    int weekday = bcd_decode(regs[3] & 0x07u);
    int day = bcd_decode(regs[4] & 0x3fu);
    int month = bcd_decode(regs[5] & 0x1fu);
    int year2 = bcd_decode(regs[6]);
    int year = year2 < 0 ? -1 : 2000 + year2;

    if ((regs[5] & DS3231_MONTH_CENTURY) != 0u || weekday < 1 || weekday > 7) {
        return MINI_ERR_IO;
    }
    return calendar_to_utc(year, month, day, hour, minute, second,
                           out_seconds, out_nanoseconds);
}

static mini_result_t hym8563_load_utc(int64_t *out_seconds, uint32_t *out_nanoseconds)
{
    uint8_t regs[7];
    if (!read_block(HYM8563_REG_SECONDS, regs, sizeof(regs))) return MINI_ERR_IO;
    if ((regs[0] & HYM8563_SECONDS_VL) != 0u) return MINI_ERR_NOT_READY;

    int second = bcd_decode(regs[0] & 0x7fu);
    int minute = bcd_decode(regs[1] & 0x7fu);
    int hour = bcd_decode(regs[2] & 0x3fu);
    int day = bcd_decode(regs[3] & 0x3fu);
    int weekday = bcd_decode(regs[4] & 0x07u);
    int month = bcd_decode(regs[5] & 0x1fu);
    int year2 = bcd_decode(regs[6]);
    int year = year2 < 0 ? -1 : 2000 + year2;

    /* HYM8563/BM8563 weekday is 0..6. The century bit is intentionally
     * ignored; MiniShell currently supports 2000..2099 only. */
    if (weekday < 0 || weekday > 6) return MINI_ERR_IO;
    return calendar_to_utc(year, month, day, hour, minute, second,
                           out_seconds, out_nanoseconds);
}

static mini_result_t split_utc(int64_t seconds, uint32_t nanoseconds,
                               int *year, unsigned *month, unsigned *day,
                               unsigned *hour, unsigned *minute, unsigned *second,
                               int64_t *out_days)
{
    if (nanoseconds >= 1000000000u) return MINI_ERR_INVALID;

    int64_t days = seconds / 86400;
    int64_t sod = seconds % 86400;
    if (sod < 0) {
        sod += 86400;
        --days;
    }

    civil_from_days(days, year, month, day);
    if (*year < 2000 || *year > 2099) return MINI_ERR_INVALID;

    *hour = (unsigned)(sod / 3600);
    *minute = (unsigned)((sod % 3600) / 60);
    *second = (unsigned)(sod % 60);
    *out_days = days;
    return MINI_OK;
}

static mini_result_t ds3231_store_utc(int64_t seconds, uint32_t nanoseconds)
{
    int year;
    unsigned month;
    unsigned day;
    unsigned hour;
    unsigned minute;
    unsigned second;
    int64_t days;
    mini_result_t split = split_utc(seconds, nanoseconds, &year, &month, &day,
                                    &hour, &minute, &second, &days);
    if (split != MINI_OK) return split;

    int64_t weekday_zero = (days + 4) % 7;
    if (weekday_zero < 0) weekday_zero += 7;

    uint8_t regs[7] = {
        bcd_encode(second),
        bcd_encode(minute),
        bcd_encode(hour),
        bcd_encode((unsigned)weekday_zero + 1u),
        bcd_encode(day),
        bcd_encode(month),
        bcd_encode((unsigned)(year - 2000)),
    };

    if (!write_block(DS3231_REG_SECONDS, regs, sizeof(regs))) return MINI_ERR_IO;

    uint8_t status = 0u;
    if (!read_block(DS3231_REG_STATUS, &status, 1u)) return MINI_ERR_IO;
    if (!write_byte(DS3231_REG_STATUS, (uint8_t)(status & ~DS3231_STATUS_OSF))) {
        return MINI_ERR_IO;
    }
    return MINI_OK;
}

static mini_result_t hym8563_store_utc(int64_t seconds, uint32_t nanoseconds)
{
    int year;
    unsigned month;
    unsigned day;
    unsigned hour;
    unsigned minute;
    unsigned second;
    int64_t days;
    mini_result_t split = split_utc(seconds, nanoseconds, &year, &month, &day,
                                    &hour, &minute, &second, &days);
    if (split != MINI_OK) return split;

    int64_t weekday_zero = (days + 4) % 7;
    if (weekday_zero < 0) weekday_zero += 7;

    uint8_t regs[7] = {
        bcd_encode(second),
        bcd_encode(minute),
        bcd_encode(hour),
        bcd_encode(day),
        bcd_encode((unsigned)weekday_zero),
        bcd_encode(month),
        bcd_encode((unsigned)(year - 2000)),
    };

    if (!write_byte(HYM8563_REG_CONTROL1, HYM8563_CONTROL1_STOP)) return MINI_ERR_IO;
    if (!write_block(HYM8563_REG_SECONDS, regs, sizeof(regs))) {
        (void)write_byte(HYM8563_REG_CONTROL1, 0u);
        return MINI_ERR_IO;
    }
    if (!write_byte(HYM8563_REG_CONTROL1, 0u)) return MINI_ERR_IO;
    return MINI_OK;
}

int adv_rtc_prepare(void)
{
    if (s_kind != ADV_RTC_NONE) return 0;
    if (adv_i2c_prepare() != 0) return -1;

    /* Keep DS3231 as the first choice to preserve existing ADV behavior.
     * M5 Unit RTC uses a different address, so either module works without
     * a user-visible configuration switch. */
    if (attach_and_probe(DS3231_ADDRESS, DS3231_REG_STATUS, ADV_RTC_DS3231)) return 0;
    if (attach_and_probe(HYM8563_ADDRESS, HYM8563_REG_CONTROL1, ADV_RTC_HYM8563)) return 0;
    return -1;
}

bool adv_rtc_ready(void)
{
    return s_kind != ADV_RTC_NONE;
}

mini_result_t adv_rtc_load_utc(int64_t *out_seconds, uint32_t *out_nanoseconds)
{
    if (out_seconds == NULL || out_nanoseconds == NULL) return MINI_ERR_INVALID;

    switch (s_kind) {
        case ADV_RTC_DS3231:
            return ds3231_load_utc(out_seconds, out_nanoseconds);
        case ADV_RTC_HYM8563:
            return hym8563_load_utc(out_seconds, out_nanoseconds);
        default:
            return MINI_ERR_NOT_READY;
    }
}

mini_result_t adv_rtc_store_utc(int64_t seconds, uint32_t nanoseconds)
{
    switch (s_kind) {
        case ADV_RTC_DS3231:
            return ds3231_store_utc(seconds, nanoseconds);
        case ADV_RTC_HYM8563:
            return hym8563_store_utc(seconds, nanoseconds);
        default:
            return MINI_ERR_NOT_READY;
    }
}
