#ifndef FT8_QSO_VIEW_H
#define FT8_QSO_VIEW_H
#include <stdint.h>

#define QSO_PAGE_ROWS 6u
#define QSO_CALL_CAP 32u
/* Presentation snapshot facts; no persistence-service dependency. */
typedef enum { QSO_VIEW_OK, QSO_VIEW_UTC_UNAVAILABLE, QSO_VIEW_READ_ERROR } QsoViewStatus;
typedef struct {
    char call[QSO_CALL_CAP];
    char band[4];
    uint8_t hour, minute;
} QsoSummary;
typedef struct {
    QsoViewStatus status;
    uint32_t total_count, page_index, page_count, row_count;
    QsoSummary rows[QSO_PAGE_ROWS];
} QsoPage;
#endif
