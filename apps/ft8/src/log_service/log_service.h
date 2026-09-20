#ifndef FT8_LOG_SERVICE_H
#define FT8_LOG_SERVICE_H

#include <stdbool.h>
#include "minishell/api.h"

#define LOG_QSO_PAGE_ROWS 6u
#define LOG_QSO_CALL_CAP 32u
/* Bounded facts parsed from the daily ADIF, with no presentation dependency. */
typedef enum { LOG_QSO_VIEW_OK, LOG_QSO_VIEW_UTC_UNAVAILABLE, LOG_QSO_VIEW_READ_ERROR } LogQsoViewStatus;
typedef struct {
    char call[LOG_QSO_CALL_CAP];
    char band[4];
    uint8_t hour, minute;
} LogQsoSummary;
typedef struct {
    LogQsoViewStatus status;
    uint32_t total_count, page_index, page_count, row_count;
    LogQsoSummary rows[LOG_QSO_PAGE_ROWS];
} LogQsoPage;

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

/* Stream today's ADIF once; clamp a stale/out-of-range request to the last
 * page. Errors are snapshot status, never application-fatal. */
void log_service_read_qso_page(const LogService *service, uint32_t page_index, LogQsoPage *out);

/* Append one canonical RT record and sync/close before returning. */
bool log_service_write_rt(const LogService *service, bool transmit, int band_index,
                           const char *text, int8_t snr_db, int16_t offset_hz);

#endif
