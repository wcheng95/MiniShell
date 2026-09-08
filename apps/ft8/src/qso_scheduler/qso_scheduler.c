#include "qso_scheduler.h"

void qso_scheduler_init(QsoScheduler *scheduler) {
    scheduler->skip_tx1 = false;
    scheduler->max_retry = 3;
}

void qso_scheduler_set_skip_tx1(QsoScheduler *scheduler, bool enabled) {
    scheduler->skip_tx1 = enabled;
}

bool qso_scheduler_get_skip_tx1(const QsoScheduler *scheduler) {
    return scheduler->skip_tx1;
}

void qso_scheduler_set_max_retry(QsoScheduler *scheduler, int value) {
    if (value < 0) value = 0;
    scheduler->max_retry = value;
}

int qso_scheduler_get_max_retry(const QsoScheduler *scheduler) {
    return scheduler->max_retry;
}
