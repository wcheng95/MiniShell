/* Linux composition of the portable app and its single bounded decode worker.
 * Native scheduling stays here, outside JS8Chat's public-service-only sources. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <time.h>
#include "js8_live.h"

static pthread_t thread;
static atomic_int stopping;
static void *decode_worker(void *context)
{
    Js8Live *live = context;
    while (!atomic_load_explicit(&stopping, memory_order_acquire)) {
        if (!js8_live_decode_step(live)) {
            const struct timespec pause = {0, 1000000};
            nanosleep(&pause, NULL);
        }
    }
    return NULL;
}
static int start(Js8Live *live)
{
    atomic_init(&stopping, 0);
    return pthread_create(&thread, NULL, decode_worker, live) ? -1 : 0;
}
static void stop(Js8Live *live)
{
    (void)live;
    atomic_store_explicit(&stopping, 1, memory_order_release);
    (void)pthread_join(thread, NULL);
}
int main(int argc, char **argv)
{
    const Js8Worker worker = {start, stop};
    return js8chat_run(mini_api_get(), argc, argv, &worker);
}
