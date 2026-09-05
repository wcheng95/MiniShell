#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minishell_services.h"

#define TEST_CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return false; \
    } \
} while (0)

#define TEST_EQ(actual, expected) TEST_CHECK((actual) == (expected))

typedef struct {
    char path[128];
    bool exists;
    bool is_dir;
    uint8_t data[2048];
    uint32_t size;
} fake_fs_node_t;

typedef struct {
    bool used;
    uint32_t node;
    uint32_t pos;
    uint32_t flags;
} fake_fs_handle_t;

typedef struct {
    char output[4096];
    uint32_t output_len;
    bool fail_alloc;
    bool fail_realloc;
    uint32_t alloc_calls;
    uint32_t realloc_calls;
    uint32_t free_calls;
    uint64_t reported_free;
    uint64_t reported_largest;
    bool report_memory_info;
    fake_fs_node_t fs_nodes[16];
    fake_fs_handle_t fs_handles[40];
    uint32_t fs_max_read;
    uint32_t fs_max_write;
    bool fs_fail_seek;
    uint32_t fs_open_calls;
    uint32_t fs_close_calls;
    uint32_t fs_sync_calls;
    char fs_last_path[128];
    uint64_t mono_us;
    uint32_t sleep_calls;
    bool utc_present;
    int64_t utc_seconds;
    uint32_t utc_nanoseconds;
    bool utc_store_fail;
    uint32_t utc_store_calls;
    bool default_present;
    int32_t default_lat;
    int32_t default_lon;
    bool default_store_fail;
    bool default_clear_fail;
    uint32_t default_store_calls;
    uint32_t default_clear_calls;
    uint32_t display_columns;
    uint32_t display_rows;
    char display_cells[8][32];
    uint32_t display_clear_calls;
    uint32_t display_present_calls;
    uint32_t display_last_clear_row;
    uint32_t display_last_clear_col;
    uint32_t display_last_clear_rows;
    uint32_t display_last_clear_cols;
    uint32_t display_last_write_count;
    bool input_inject_on_wait;
    mini_key_event_t input_injected_event;
    uint32_t input_wait_calls;
    uint32_t input_wake_calls;
} fake_state_t;

extern fake_state_t g_fake;
void fake_reset(void);
minishell_services_port_t fake_full_port(void);
minishell_services_port_t fake_minimal_port(void);
void fake_fs_add_file(const char *path, const char *content);
void fake_fs_add_dir(const char *path);

bool test_system(void);
bool test_memory(void);
bool test_filesystem(void);
bool test_time_location(void);
bool test_display(void);
bool test_input(void);
bool test_transfer(void);
