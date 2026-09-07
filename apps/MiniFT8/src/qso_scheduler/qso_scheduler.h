#ifndef MINIFT8_QSO_SCHEDULER_H
#define MINIFT8_QSO_SCHEDULER_H

#include <stdbool.h>

typedef struct {
    bool skip_tx1;
    int max_retry;
} QsoScheduler;

void qso_scheduler_init(QsoScheduler *scheduler);
void qso_scheduler_set_skip_tx1(QsoScheduler *scheduler, bool enabled);
bool qso_scheduler_get_skip_tx1(const QsoScheduler *scheduler);
void qso_scheduler_set_max_retry(QsoScheduler *scheduler, int value);
int qso_scheduler_get_max_retry(const QsoScheduler *scheduler);

#endif
