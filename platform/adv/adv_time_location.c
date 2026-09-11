#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "adv_internal.h"
#include "adv_rtc.h"

#define ADV_STATE_DIR "/flash/minishell"
#define ADV_LOCATION_PATH ADV_STATE_DIR "/location.txt"
#define ADV_LOCATION_TEMP ADV_STATE_DIR "/location.tmp"

/* Deterministic ADV bootstrap when no valid persistent RTC sample exists. */
#define ADV_DEFAULT_UTC_SECONDS ((int64_t)1788242400) /* 2026-09-01 06:00:00 UTC */

static mini_result_t result_from_errno(int error)
{
    switch (error) {
        case 0: return MINI_OK;
        case ENOENT: return MINI_ERR_NOT_FOUND;
        case EACCES:
        case EPERM:
        case EROFS: return MINI_ERR_ACCESS;
        case ENOSPC: return MINI_ERR_NO_SPACE;
        case ENOMEM: return MINI_ERR_NO_MEMORY;
        default: return MINI_ERR_IO;
    }
}

static mini_result_t commit_file(FILE *file, const char *temp_path, const char *path)
{
    if (fflush(file) != 0) {
        int error = errno;
        (void)fclose(file);
        (void)unlink(temp_path);
        return result_from_errno(error);
    }
    int fd = fileno(file);
    if (fd >= 0 && fsync(fd) != 0) {
        int error = errno;
        (void)fclose(file);
        (void)unlink(temp_path);
        return result_from_errno(error);
    }
    if (fclose(file) != 0) {
        int error = errno;
        (void)unlink(temp_path);
        return result_from_errno(error);
    }
    if (rename(temp_path, path) != 0) {
        int error = errno;
        (void)unlink(temp_path);
        return result_from_errno(error);
    }
    return MINI_OK;
}

static mini_result_t utc_load(void *ctx, int64_t *out_seconds,
                              uint32_t *out_nanoseconds)
{
    (void)ctx;
    if (out_seconds == NULL || out_nanoseconds == NULL) return MINI_ERR_INVALID;

    if (adv_rtc_ready()) {
        mini_result_t result = adv_rtc_load_utc(out_seconds, out_nanoseconds);
        if (result == MINI_OK) return MINI_OK;
        if (result != MINI_ERR_NOT_READY) return result;
    }

    *out_seconds = ADV_DEFAULT_UTC_SECONDS;
    *out_nanoseconds = 0u;
    return MINI_OK;
}

static mini_result_t utc_store(void *ctx, int64_t seconds, uint32_t nanoseconds)
{
    (void)ctx;
    return adv_rtc_store_utc(seconds, nanoseconds);
}

static mini_result_t default_location_load(void *ctx,
                                           int32_t *out_latitude_e7,
                                           int32_t *out_longitude_e7)
{
    (void)ctx;
    if (out_latitude_e7 == NULL || out_longitude_e7 == NULL) return MINI_ERR_INVALID;
    FILE *file = fopen(ADV_LOCATION_PATH, "r");
    if (file == NULL) return result_from_errno(errno);

    long latitude = 0;
    long longitude = 0;
    int matched = fscanf(file, "%ld %ld", &latitude, &longitude);
    int close_result = fclose(file);
    if (matched != 2 || close_result != 0 || latitude < INT32_MIN ||
        latitude > INT32_MAX || longitude < INT32_MIN || longitude > INT32_MAX) {
        return MINI_ERR_IO;
    }
    *out_latitude_e7 = (int32_t)latitude;
    *out_longitude_e7 = (int32_t)longitude;
    return MINI_OK;
}

static mini_result_t default_location_store(void *ctx,
                                            int32_t latitude_e7,
                                            int32_t longitude_e7)
{
    (void)ctx;
    FILE *file = fopen(ADV_LOCATION_TEMP, "w");
    if (file == NULL) return result_from_errno(errno);
    if (fprintf(file, "%ld %ld\n", (long)latitude_e7, (long)longitude_e7) < 0) {
        int error = errno;
        (void)fclose(file);
        (void)unlink(ADV_LOCATION_TEMP);
        return result_from_errno(error);
    }
    return commit_file(file, ADV_LOCATION_TEMP, ADV_LOCATION_PATH);
}

static mini_result_t default_location_clear(void *ctx)
{
    (void)ctx;
    if (unlink(ADV_LOCATION_PATH) == 0 || errno == ENOENT) return MINI_OK;
    return result_from_errno(errno);
}

void adv_time_location_configure(minishell_services_port_t *port)
{
    if (port == NULL) return;

    port->time_location_capabilities = MINI_TIMELOC_CAP_UTC;
    port->utc_load = utc_load;
    if (adv_rtc_ready()) port->utc_store = utc_store;

    if (!adv_filesystem_flash_ready()) return;

    port->time_location_capabilities |=
        MINI_TIMELOC_CAP_LOCATION |
        MINI_TIMELOC_CAP_DEFAULT_LOCATION |
        MINI_TIMELOC_CAP_SET_DEFAULT_LOCATION;
    port->default_location_load = default_location_load;
    port->default_location_store = default_location_store;
    port->default_location_clear = default_location_clear;
}
