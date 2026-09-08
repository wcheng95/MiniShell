#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "linux_internal.h"

static uint64_t monotonic_us(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0u;
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static mini_result_t sleep_ms(void *ctx, uint32_t milliseconds)
{
    (void)ctx;
    struct timespec request = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L,
    };
    while (nanosleep(&request, &request) != 0) {
        if (errno != EINTR) return linux_result_from_errno(errno);
    }
    return MINI_OK;
}

static mini_result_t utc_load(void *ctx, int64_t *out_seconds,
                              uint32_t *out_nanoseconds)
{
    (void)ctx;
    if (out_seconds == NULL || out_nanoseconds == NULL) return MINI_ERR_INVALID;

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return linux_result_from_errno(errno);
    *out_seconds = (int64_t)ts.tv_sec;
    *out_nanoseconds = (uint32_t)ts.tv_nsec;
    return MINI_OK;
}

static mini_result_t location_path(char *out, size_t out_size)
{
    int written = snprintf(out, out_size, "%s/.state/location", linux_root_dir());
    return written < 0 || (size_t)written >= out_size
               ? MINI_ERR_NAME_TOO_LONG
               : MINI_OK;
}

static mini_result_t default_location_load(void *ctx,
                                           int32_t *out_latitude_e7,
                                           int32_t *out_longitude_e7)
{
    (void)ctx;
    if (out_latitude_e7 == NULL || out_longitude_e7 == NULL) return MINI_ERR_INVALID;

    char path[PATH_MAX];
    mini_result_t result = location_path(path, sizeof(path));
    if (result != MINI_OK) return result;

    FILE *file = fopen(path, "r");
    if (file == NULL) return linux_result_from_errno(errno);

    long latitude;
    long longitude;
    int matched = fscanf(file, "%ld %ld", &latitude, &longitude);
    int close_result = fclose(file);
    if (matched != 2 || close_result != 0 ||
        latitude < INT32_MIN || latitude > INT32_MAX ||
        longitude < INT32_MIN || longitude > INT32_MAX) {
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
    char path[PATH_MAX];
    mini_result_t result = location_path(path, sizeof(path));
    if (result != MINI_OK) return result;

    FILE *file = fopen(path, "w");
    if (file == NULL) return linux_result_from_errno(errno);
    if (fprintf(file, "%ld %ld\n", (long)latitude_e7, (long)longitude_e7) < 0 ||
        fflush(file) != 0) {
        int error = errno;
        (void)fclose(file);
        return linux_result_from_errno(error);
    }

    int fd = fileno(file);
    if (fd >= 0 && fsync(fd) != 0) {
        int error = errno;
        (void)fclose(file);
        return linux_result_from_errno(error);
    }
    return fclose(file) == 0 ? MINI_OK : linux_result_from_errno(errno);
}

static mini_result_t default_location_clear(void *ctx)
{
    (void)ctx;
    char path[PATH_MAX];
    mini_result_t result = location_path(path, sizeof(path));
    if (result != MINI_OK) return result;
    if (unlink(path) == 0 || errno == ENOENT) return MINI_OK;
    return linux_result_from_errno(errno);
}

void linux_time_location_configure(minishell_services_port_t *port)
{
    if (port == NULL) return;
    port->monotonic_us = monotonic_us;
    port->sleep_ms = sleep_ms;
    port->time_location_capabilities =
        MINI_TIMELOC_CAP_UTC |
        MINI_TIMELOC_CAP_LOCATION |
        MINI_TIMELOC_CAP_DEFAULT_LOCATION |
        MINI_TIMELOC_CAP_SET_DEFAULT_LOCATION;
    port->utc_load = utc_load;
    port->default_location_load = default_location_load;
    port->default_location_store = default_location_store;
    port->default_location_clear = default_location_clear;
}
