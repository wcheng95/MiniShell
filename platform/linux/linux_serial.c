#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "linux_internal.h"

static int s_fd = -1;
static struct termios s_saved;
#define SERIAL_HANDLE ((minishell_backend_serial_t)1u)

static mini_result_t serial_open(void *ctx, const char *endpoint,
                                 minishell_backend_serial_t *out)
{
    (void)ctx;
    if (!out) return MINI_ERR_INVALID;
    *out = MINISHELL_BACKEND_SERIAL_INVALID;
    if (!endpoint || strncmp(endpoint, "serial:/", 8) != 0 || !endpoint[8])
        return MINI_ERR_INVALID;
    if (s_fd >= 0) return MINI_ERR_TOO_MANY_OPEN;
    int fd = open(endpoint + 7, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return linux_result_from_errno(errno);
    struct termios saved, raw;
    if (tcgetattr(fd, &saved) != 0) { close(fd); return MINI_ERR_UNSUPPORTED; }
    raw = saved;
    cfmakeraw(&raw);
    raw.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
    raw.c_cflag |= CS8 | CLOCAL | CREAD;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (cfsetispeed(&raw, B115200) != 0 || cfsetospeed(&raw, B115200) != 0 ||
        tcsetattr(fd, TCSANOW, &raw) != 0) {
        (void)tcsetattr(fd, TCSANOW, &saved);
        close(fd);
        return MINI_ERR_IO;
    }
    s_saved = saved;
    s_fd = fd;
    *out = SERIAL_HANDLE;
    return MINI_OK;
}

static int64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static mini_result_t transfer(minishell_backend_serial_t serial, void *read_buffer,
                               const void *write_buffer, uint32_t size,
                               uint32_t *out, uint32_t timeout, bool writing)
{
    if (!out) return MINI_ERR_INVALID;
    *out = 0;
    if (serial != SERIAL_HANDLE || s_fd < 0) return MINI_ERR_BAD_HANDLE;
    if (!size) return MINI_OK;
    if (writing ? !write_buffer : !read_buffer) return MINI_ERR_INVALID;
    int64_t now = monotonic_ms();
    if (now < 0) return MINI_ERR_IO;
    int64_t deadline = now + timeout;
    for (;;) {
        ssize_t n = writing ? write(s_fd, write_buffer, size) : read(s_fd, read_buffer, size);
        if (n > 0) { *out = (uint32_t)n; return MINI_OK; }
        if (n == 0) return MINI_ERR_IO;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            return linux_result_from_errno(errno);

        now = monotonic_ms();
        if (now < 0) return MINI_ERR_IO;
        if (timeout != MINI_WAIT_FOREVER && now >= deadline) return MINI_ERR_TIMEOUT;
        int64_t remaining = deadline - now;
        int wait = timeout == MINI_WAIT_FOREVER ? -1 :
                   remaining > INT_MAX ? INT_MAX : (int)remaining;
        struct pollfd event = {.fd = s_fd, .events = writing ? POLLOUT : POLLIN};
        int ready = poll(&event, 1, wait);
        if (ready < 0 && errno != EINTR) return linux_result_from_errno(errno);
        if (ready > 0 && (event.revents & (POLLERR | POLLHUP | POLLNVAL))) return MINI_ERR_IO;
        /* Retry against the same deadline after signals/spurious readiness. */
    }
}

static mini_result_t serial_read(void *ctx, minishell_backend_serial_t serial,
                                  void *buffer, uint32_t size, uint32_t *out, uint32_t timeout)
{ (void)ctx; return transfer(serial, buffer, NULL, size, out, timeout, false); }

static mini_result_t serial_write(void *ctx, minishell_backend_serial_t serial,
                                   const void *buffer, uint32_t size, uint32_t *out, uint32_t timeout)
{ (void)ctx; return transfer(serial, NULL, buffer, size, out, timeout, true); }

static mini_result_t serial_close(void *ctx, minishell_backend_serial_t serial)
{
    (void)ctx;
    if (serial != SERIAL_HANDLE || s_fd < 0) return MINI_ERR_BAD_HANDLE;
    int fd = s_fd;
    s_fd = -1;
    int restored = tcsetattr(fd, TCSANOW, &s_saved);
    /* Linux releases the descriptor even on close errors; never retry close. */
    int closed = close(fd);
    return restored == 0 && closed == 0 ? MINI_OK : MINI_ERR_IO;
}

void linux_serial_configure(minishell_services_port_t *port)
{
    port->serial_capabilities = MINI_SERIAL_CAP_READ | MINI_SERIAL_CAP_WRITE;
    port->serial_open = serial_open;
    port->serial_read = serial_read;
    port->serial_write = serial_write;
    port->serial_close = serial_close;
}
