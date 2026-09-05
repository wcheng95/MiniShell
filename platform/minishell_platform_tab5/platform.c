#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "minishell_services.h"
#include "minishell_transfer.h"
#include "minishell_platform.h"
#include "terminal_backend.h"

#define TRANSFER_REPLACE_PATH_MAX 600u
#define CONSOLE_RX_BUFFER_SIZE    4096u
#define CONSOLE_TX_BUFFER_SIZE    4096u

esp_err_t bsp_sdcard_mount(void);

static esp_err_t s_console_status = ESP_FAIL;
static esp_err_t s_sd_status = ESP_FAIL;

static mini_result_t errno_to_mini(int err)
{
    switch (err) {
    case 0: return MINI_OK;
    case EINVAL: return MINI_ERR_INVALID;
    case ENOENT: return MINI_ERR_NOT_FOUND;
    case EEXIST: return MINI_ERR_EXISTS;
    case EACCES:
    case EPERM: return MINI_ERR_ACCESS;
    case ENOSPC: return MINI_ERR_NO_SPACE;
    case EMFILE:
    case ENFILE: return MINI_ERR_TOO_MANY_OPEN;
    case ENAMETOOLONG: return MINI_ERR_NAME_TOO_LONG;
    case ENOTDIR: return MINI_ERR_NOT_DIR;
    case EISDIR: return MINI_ERR_IS_DIR;
    case ENOMEM: return MINI_ERR_NO_MEMORY;
#ifdef ENOTSUP
    case ENOTSUP: return MINI_ERR_UNSUPPORTED;
#endif
#if defined(EOPNOTSUPP) && (!defined(ENOTSUP) || EOPNOTSUPP != ENOTSUP)
    case EOPNOTSUPP: return MINI_ERR_UNSUPPORTED;
#endif
    default: return MINI_ERR_IO;
    }
}

static esp_err_t init_console(void)
{
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        config.rx_buffer_size = CONSOLE_RX_BUFFER_SIZE;
        config.tx_buffer_size = CONSOLE_TX_BUFFER_SIZE;
        esp_err_t err = usb_serial_jtag_driver_install(&config);
        if (err != ESP_OK) return err;
    }

    usb_serial_jtag_vfs_use_driver();
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    return ESP_OK;
}

static TickType_t console_timeout_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == MINI_WAIT_FOREVER) return portMAX_DELAY;
    if (timeout_ms == MINI_WAIT_NONE) return 0;
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    return ticks == 0 ? 1 : ticks;
}

static int terminal_read_byte(uint32_t timeout_ms)
{
    uint8_t byte = 0u;
    int count = usb_serial_jtag_read_bytes(&byte, 1u, console_timeout_ticks(timeout_ms));
    return count == 1 ? (int)byte : -1;
}

static int transfer_read(void *ctx, uint8_t *buffer, size_t size, uint32_t timeout_ms)
{
    (void)ctx;
    if (buffer == NULL || size == 0u) return 0;
    return usb_serial_jtag_read_bytes(buffer, size, console_timeout_ticks(timeout_ms));
}

static int transfer_write(void *ctx, const uint8_t *buffer, size_t size, uint32_t timeout_ms)
{
    (void)ctx;
    if (buffer == NULL || size == 0u) return 0;
    return usb_serial_jtag_write_bytes(buffer, size, console_timeout_ticks(timeout_ms));
}

static int transfer_replace_file(void *ctx, const char *temporary_path,
                                 const char *destination_path)
{
    (void)ctx;
    if (temporary_path == NULL || destination_path == NULL) return -EINVAL;

    if (rename(temporary_path, destination_path) == 0) return 0;
    if (errno != EEXIST) return -errno;

    char backup[TRANSFER_REPLACE_PATH_MAX];
    int n = snprintf(backup, sizeof(backup), "%s.mft.bak", destination_path);
    if (n < 0 || (size_t)n >= sizeof(backup)) return -ENAMETOOLONG;

    (void)unlink(backup);
    if (rename(destination_path, backup) != 0) return -errno;

    if (rename(temporary_path, destination_path) == 0) {
        (void)unlink(backup);
        return 0;
    }

    int publish_error = errno;
    (void)rename(backup, destination_path);
    return -publish_error;
}

static void transfer_remove_file(void *ctx, const char *path)
{
    (void)ctx;
    if (path == NULL) return;
    if (unlink(path) != 0 && errno != ENOENT) {
    }
}

static esp_err_t mount_sd(void)
{
    static const char *const bsp_tag = "M5Stack Tab5";
    esp_log_level_t old_level = esp_log_level_get(bsp_tag);
    esp_log_level_set(bsp_tag, ESP_LOG_ERROR);
    esp_err_t result = bsp_sdcard_mount();
    esp_log_level_set(bsp_tag, old_level);
    return result;
}

static void service_system_write(void *ctx, const char *text)
{
    (void)ctx;
    fputs(text, stdout);
    fflush(stdout);
}

static void *service_memory_alloc(void *ctx, uint32_t size)
{
    (void)ctx;
    return malloc((size_t)size);
}

static void *service_memory_realloc(void *ctx, void *ptr, uint32_t new_size)
{
    (void)ctx;
    return realloc(ptr, (size_t)new_size);
}

static void service_memory_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

static int backend_fd(minishell_backend_file_t file)
{
    return (int)(file - 1u);
}

static mini_result_t service_fs_open(void *ctx, const char *path, uint32_t flags,
                                     minishell_backend_file_t *out_file)
{
    (void)ctx;
    int oflags = 0;
    if ((flags & MINI_FS_READ) && (flags & MINI_FS_WRITE)) oflags |= O_RDWR;
    else if (flags & MINI_FS_WRITE) oflags |= O_WRONLY;
    else oflags |= O_RDONLY;
    if (flags & MINI_FS_CREATE) oflags |= O_CREAT;
    if (flags & MINI_FS_EXCL) oflags |= O_EXCL;
    if (flags & MINI_FS_TRUNC) oflags |= O_TRUNC;
    if (flags & MINI_FS_APPEND) oflags |= O_APPEND;

    int fd = open(path, oflags, 0666);
    if (fd < 0) return errno_to_mini(errno);
    *out_file = (minishell_backend_file_t)((uintptr_t)fd + 1u);
    return MINI_OK;
}

static mini_result_t service_fs_close(void *ctx, minishell_backend_file_t file)
{
    (void)ctx;
    return close(backend_fd(file)) == 0 ? MINI_OK : errno_to_mini(errno);
}

static mini_result_t service_fs_read(void *ctx, minishell_backend_file_t file,
                                     void *buffer, uint32_t size, uint32_t *out_read)
{
    (void)ctx;
    ssize_t n = read(backend_fd(file), buffer, (size_t)size);
    if (n < 0) return errno_to_mini(errno);
    *out_read = (uint32_t)n;
    return MINI_OK;
}

static mini_result_t service_fs_write(void *ctx, minishell_backend_file_t file,
                                      const void *buffer, uint32_t size,
                                      uint32_t *out_written)
{
    (void)ctx;
    ssize_t n = write(backend_fd(file), buffer, (size_t)size);
    if (n < 0) return errno_to_mini(errno);
    *out_written = (uint32_t)n;
    return MINI_OK;
}

static mini_result_t service_fs_seek(void *ctx, minishell_backend_file_t file,
                                     int64_t offset, uint32_t origin,
                                     uint64_t *out_position)
{
    (void)ctx;
    int whence = origin == MINI_FS_SEEK_SET ? SEEK_SET :
                 origin == MINI_FS_SEEK_CUR ? SEEK_CUR : SEEK_END;
    off_t requested = (off_t)offset;
    if ((int64_t)requested != offset) return MINI_ERR_UNSUPPORTED;
    off_t result = lseek(backend_fd(file), requested, whence);
    if (result < 0) return errno_to_mini(errno);
    *out_position = (uint64_t)result;
    return MINI_OK;
}

static mini_result_t service_fs_sync(void *ctx, minishell_backend_file_t file)
{
    (void)ctx;
    return fsync(backend_fd(file)) == 0 ? MINI_OK : errno_to_mini(errno);
}

static mini_result_t service_fs_stat(void *ctx, const char *path,
                                     uint32_t *out_type, uint64_t *out_size)
{
    (void)ctx;
    struct stat st;
    if (stat(path, &st) != 0) return errno_to_mini(errno);
    if (S_ISREG(st.st_mode)) *out_type = MINI_FS_TYPE_FILE;
    else if (S_ISDIR(st.st_mode)) *out_type = MINI_FS_TYPE_DIRECTORY;
    else return MINI_ERR_UNSUPPORTED;
    *out_size = (uint64_t)st.st_size;
    return MINI_OK;
}

static uint64_t service_monotonic_us(void *ctx)
{
    (void)ctx;
    return (uint64_t)esp_timer_get_time();
}

static mini_result_t service_sleep_ms(void *ctx, uint32_t milliseconds)
{
    (void)ctx;
    if (milliseconds == 0u) return MINI_OK;
    TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    if (ticks == 0u) ticks = 1u;
    vTaskDelay(ticks);
    return MINI_OK;
}

static void service_input_flush(void *ctx)
{
    (void)ctx;
    minishell_terminal_input_flush();
}

static void configure_services(void)
{
    bool terminal_ready = s_console_status == ESP_OK;
    minishell_terminal_backend_init(terminal_ready ? terminal_read_byte : NULL);

    const minishell_services_port_t port = {
        .ctx = NULL,
        .system_write = service_system_write,
        .memory_alloc = service_memory_alloc,
        .memory_realloc = service_memory_realloc,
        .memory_free = service_memory_free,
        .memory_get_info = NULL,
        .fs_open = service_fs_open,
        .fs_close = service_fs_close,
        .fs_read = service_fs_read,
        .fs_write = service_fs_write,
        .fs_seek = service_fs_seek,
        .fs_sync = service_fs_sync,
        .fs_stat = service_fs_stat,
        .monotonic_us = service_monotonic_us,
        .sleep_ms = service_sleep_ms,
        .time_location_capabilities = 0u,
        .display_capabilities = terminal_ready ? MINI_DISPLAY_CAP_TEXT : 0u,
        .display_text_get_info = minishell_terminal_display_get_info,
        .display_text_clear = minishell_terminal_display_clear,
        .display_text_clear_at = minishell_terminal_display_clear_at,
        .display_text_write_at = minishell_terminal_display_write_at,
        .display_present = minishell_terminal_display_present,
        .input_capabilities = terminal_ready ? MINI_INPUT_CAP_KEY : 0u,
        .input_wait = minishell_terminal_input_wait,
        .input_flush = service_input_flush,
    };

    minishell_services_configure(&port);

    if (terminal_ready) {
        const minishell_transfer_port_t transfer_port = {
            .ctx = NULL,
            .read = transfer_read,
            .write = transfer_write,
            .replace_file = transfer_replace_file,
            .remove_file = transfer_remove_file,
        };
        minishell_transfer_configure(&transfer_port);
    } else {
        minishell_transfer_configure(NULL);
    }
}

int minishell_platform_init(void)
{
    int result = 0;

    s_console_status = init_console();
    if (s_console_status != ESP_OK) {
        printf("console: setup failed: %s (0x%x)\n",
               esp_err_to_name(s_console_status),
               (unsigned int)s_console_status);
        result = -1;
    }

    s_sd_status = mount_sd();
    if (s_sd_status == ESP_OK) {
        printf("sd: mounted at /sd\n");
    } else {
        printf("sd: mount failed: %s (0x%x)\n",
               esp_err_to_name(s_sd_status),
               (unsigned int)s_sd_status);
        result = -1;
    }

    configure_services();
    return result;
}

bool minishell_platform_sd_ready(void)
{
    return s_sd_status == ESP_OK;
}

const char *minishell_platform_sd_status(void)
{
    return s_sd_status == ESP_OK ? "OK" : esp_err_to_name(s_sd_status);
}

const char *minishell_platform_console_status(void)
{
    if (s_console_status == ESP_OK) {
        return "OK - USB Serial/JTAG (interrupt-driven)";
    }
    return esp_err_to_name(s_console_status);
}

const char *minishell_platform_name(void)
{
    return "M5Stack Tab5 / ESP32-P4";
}
