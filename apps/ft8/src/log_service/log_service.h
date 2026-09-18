#ifndef FT8_LOG_SERVICE_H
#define FT8_LOG_SERVICE_H

#include <stdbool.h>
#include "minishell/api.h"

typedef struct {
    const mini_fs_api_t *fs;
    const mini_time_location_api_t *time_location;
    char path_prefix[256];
} LogService;

/* Borrowed strings are non-NULL, NUL-terminated, and immutable during a write. */
typedef struct {
    const char *callsign;
    const char *effective_grid;
    const char *fd_exchange;
    int band_index;
} LogStationFacts;

typedef struct {
    const char *dxcall;
    const char *dxgrid;
    const char *fd_rx_exchange;
    int8_t snr_tx;
    int8_t snr_rx;
    bool snr_tx_known;
    bool snr_rx_known;
} LogQsoFacts;

/* Preserve the existing log directory: the parent of the station file. */
bool log_service_init(LogService *service, const mini_fs_api_t *fs,
                      const mini_time_location_api_t *time_location,
                      const char *station_path);
bool log_service_write_adif(const LogService *service, const LogStationFacts *station,
                            const LogQsoFacts *event);
bool log_service_write_cabrillo(const LogService *service, const LogStationFacts *station,
                                const LogQsoFacts *event);

#endif
