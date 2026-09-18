#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "linux_internal.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
static int64_t milliseconds(void)
{
    struct timespec now;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
int main(void)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    const char *name = ptsname(master);
    CHECK(name);
    int slave = open(name, O_RDWR | O_NOCTTY);
    CHECK(slave >= 0);
    struct termios original, raw, restored;
    CHECK(tcgetattr(slave, &original) == 0);
    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint), "serial:%s", name);
    minishell_services_port_t port = {0};
    linux_serial_configure(&port);
    minishell_services_configure(&port);
    const mini_serial_api_t *api = mini_api_get()->serial;
    CHECK(api);
    mini_serial_t stream, stale;
    CHECK(api->open("ttyACM0", &stream) == MINI_ERR_INVALID);
    CHECK(api->open("serial:", &stream) == MINI_ERR_INVALID);
    CHECK(api->open("serial:relative", &stream) == MINI_ERR_INVALID);
    CHECK(api->open("serial:/", &stream) == MINI_ERR_INVALID);
    CHECK(api->open("serial:/dev/null", &stream) == MINI_ERR_UNSUPPORTED);
    CHECK(api->open("serial:/dev/minishell-no-such-tty", &stream) == MINI_ERR_NOT_FOUND);
    CHECK(api->open(endpoint, &stream) == MINI_OK);
    CHECK(tcgetattr(slave, &raw) == 0);
    CHECK(!(raw.c_lflag & (ECHO | ICANON | ISIG)) && !(raw.c_oflag & OPOST));
    CHECK((raw.c_cflag & CSIZE) == CS8 && !(raw.c_cflag & (PARENB | CSTOPB | CRTSCTS)));
    CHECK(cfgetospeed(&raw) == B115200);
    unsigned char bytes[] = {0, 13, 10, 17, 19, 0xff}, received[32];
    uint32_t n;
    CHECK(api->write(stream, bytes, sizeof(bytes), &n, 100) == MINI_OK && n == sizeof(bytes));
    CHECK(read(master, received, sizeof(received)) == sizeof(bytes));
    CHECK(memcmp(bytes, received, sizeof(bytes)) == 0);
    CHECK(write(master, bytes, sizeof(bytes)) == sizeof(bytes));
    CHECK(api->read(stream, received, sizeof(received), &n, MINI_WAIT_FOREVER) == MINI_OK);
    CHECK(n == sizeof(bytes) && memcmp(bytes, received, n) == 0);
    CHECK(api->read(stream, received, sizeof(received), &n, MINI_WAIT_NONE) == MINI_ERR_TIMEOUT);
    int64_t before = milliseconds();
    CHECK(api->read(stream, received, sizeof(received), &n, 20) == MINI_ERR_TIMEOUT && n == 0);
    CHECK(milliseconds() - before >= 15 && milliseconds() - before < 1000);
    /* Fill the tty output queue; a non-consuming peer must not block forever. */
    char large[4096] = {0};
    mini_result_t result = MINI_OK;
    for (unsigned i = 0; i < 4096 && result == MINI_OK; ++i)
        result = api->write(stream, large, sizeof(large), &n, MINI_WAIT_NONE);
    CHECK(result == MINI_ERR_TIMEOUT);
    before = milliseconds();
    CHECK(api->write(stream, large, sizeof(large), &n, 20) == MINI_ERR_TIMEOUT && n == 0);
    CHECK(milliseconds() - before < 1000);
    stale = stream;
    minishell_services_app_end();
    CHECK(api->read(stale, received, 1, &n, 0) == MINI_ERR_BAD_HANDLE);
    CHECK(tcgetattr(slave, &restored) == 0);
    CHECK(restored.c_iflag == original.c_iflag && restored.c_oflag == original.c_oflag &&
          restored.c_cflag == original.c_cflag && restored.c_lflag == original.c_lflag);
    CHECK(api->open(endpoint, &stream) == MINI_OK && stream != stale);
    CHECK(api->close(stream) == MINI_OK);
    CHECK(api->open(endpoint, &stream) == MINI_OK);
    close(master);
    CHECK(api->read(stream, received, 1, &n, 0) == MINI_ERR_IO);
    (void)api->close(stream); /* Restoration can fail after physical disconnect. */
    CHECK(api->close(stream) == MINI_ERR_BAD_HANDLE);
    close(slave);
    puts("Linux Serial PTY: raw bytes, timeouts, restoration, cleanup and reopen PASS");
    return 0;
}
