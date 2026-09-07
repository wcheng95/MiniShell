#include "test_support.h"

fake_state_t g_fake;

static void fake_system_write(void *ctx, const char *text)
{
    fake_state_t *f = ctx;
    size_t len = strlen(text);
    if (len > sizeof(f->output) - f->output_len - 1u) len = sizeof(f->output) - f->output_len - 1u;
    memcpy(f->output + f->output_len, text, len);
    f->output_len += (uint32_t)len;
    f->output[f->output_len] = '\0';
}

static void *fake_mem_alloc(void *ctx, uint32_t size)
{
    fake_state_t *f = ctx;
    ++f->alloc_calls;
    if (f->fail_alloc) return NULL;
    return malloc(size);
}

static void *fake_mem_realloc(void *ctx, void *ptr, uint32_t size)
{
    fake_state_t *f = ctx;
    ++f->realloc_calls;
    if (f->fail_realloc) return NULL;
    return realloc(ptr, size);
}

static void fake_mem_free(void *ctx, void *ptr)
{
    fake_state_t *f = ctx;
    ++f->free_calls;
    free(ptr);
}

static bool fake_mem_info(void *ctx, uint64_t *free_bytes, uint64_t *largest)
{
    fake_state_t *f = ctx;
    if (!f->report_memory_info) return false;
    *free_bytes = f->reported_free;
    *largest = f->reported_largest;
    return true;
}

static int find_node(const char *path)
{
    for (int i = 0; i < 16; ++i) {
        if (g_fake.fs_nodes[i].exists && strcmp(g_fake.fs_nodes[i].path, path) == 0) return i;
    }
    return -1;
}

static int alloc_node(void)
{
    for (int i = 0; i < 16; ++i) if (!g_fake.fs_nodes[i].exists) return i;
    return -1;
}

static bool parent_path(const char *path, char *out, size_t out_size)
{
    if (path == NULL || path[0] != '/' || strcmp(path, "/") == 0) return false;
    size_t len = strlen(path);
    if (len + 1u > out_size) return false;
    memcpy(out, path, len + 1u);
    char *slash = strrchr(out, '/');
    if (slash == NULL) return false;
    if (slash == out) out[1] = '\0';
    else *slash = '\0';
    return true;
}

static mini_result_t require_parent_dir(const char *path)
{
    char parent[128];
    if (!parent_path(path, parent, sizeof(parent))) return MINI_ERR_INVALID;
    int i = find_node(parent);
    if (i < 0) return MINI_ERR_NOT_FOUND;
    return g_fake.fs_nodes[i].is_dir ? MINI_OK : MINI_ERR_NOT_DIR;
}

static bool path_is_child_of(const char *path, const char *directory)
{
    size_t len = strlen(directory);
    if (strncmp(path, directory, len) != 0) return false;
    if (strcmp(directory, "/") == 0) return path[1] != '\0';
    return path[len] == '/';
}

void fake_fs_add_file(const char *path, const char *content)
{
    int i = alloc_node();
    if (i < 0) return;
    fake_fs_node_t *n = &g_fake.fs_nodes[i];
    n->exists = true;
    n->is_dir = false;
    snprintf(n->path, sizeof(n->path), "%s", path);
    n->size = (uint32_t)strlen(content);
    memcpy(n->data, content, n->size);
}

void fake_fs_add_dir(const char *path)
{
    int i = alloc_node();
    if (i < 0) return;
    fake_fs_node_t *n = &g_fake.fs_nodes[i];
    n->exists = true;
    n->is_dir = true;
    snprintf(n->path, sizeof(n->path), "%s", path);
}

static mini_result_t fake_fs_open_cb(void *ctx, const char *path, uint32_t flags, minishell_backend_file_t *out_file)
{
    fake_state_t *f = ctx;
    ++f->fs_open_calls;
    snprintf(f->fs_last_path, sizeof(f->fs_last_path), "%s", path);
    *out_file = MINISHELL_BACKEND_FILE_INVALID;
    int node_i = find_node(path);
    if (node_i >= 0 && f->fs_nodes[node_i].is_dir) return MINI_ERR_IS_DIR;
    if (node_i >= 0 && (flags & MINI_FS_CREATE) && (flags & MINI_FS_EXCL)) return MINI_ERR_EXISTS;
    if (node_i < 0) {
        if (!(flags & MINI_FS_CREATE)) return MINI_ERR_NOT_FOUND;
        mini_result_t parent_result = require_parent_dir(path);
        if (parent_result != MINI_OK) return parent_result;
        node_i = alloc_node();
        if (node_i < 0) return MINI_ERR_NO_SPACE;
        fake_fs_node_t *n = &f->fs_nodes[node_i];
        n->exists = true;
        n->is_dir = false;
        snprintf(n->path, sizeof(n->path), "%s", path);
        n->size = 0u;
    }
    if (flags & MINI_FS_TRUNC) f->fs_nodes[node_i].size = 0u;
    for (uint32_t i = 0; i < 40u; ++i) {
        if (!f->fs_handles[i].used) {
            f->fs_handles[i].used = true;
            f->fs_handles[i].node = (uint32_t)node_i;
            f->fs_handles[i].pos = 0u;
            f->fs_handles[i].flags = flags;
            *out_file = (minishell_backend_file_t)(i + 1u);
            return MINI_OK;
        }
    }
    return MINI_ERR_TOO_MANY_OPEN;
}

static fake_fs_handle_t *fh(minishell_backend_file_t h)
{
    if (h == 0u || h > 40u || !g_fake.fs_handles[h - 1u].used) return NULL;
    return &g_fake.fs_handles[h - 1u];
}

static mini_result_t fake_fs_close_cb(void *ctx, minishell_backend_file_t file)
{
    fake_state_t *f = ctx;
    fake_fs_handle_t *h = fh(file);
    if (!h) return MINI_ERR_BAD_HANDLE;
    h->used = false;
    ++f->fs_close_calls;
    return MINI_OK;
}

static mini_result_t fake_fs_read_cb(void *ctx, minishell_backend_file_t file, void *buffer, uint32_t size, uint32_t *out_read)
{
    fake_state_t *f = ctx;
    fake_fs_handle_t *h = fh(file);
    if (!h) return MINI_ERR_BAD_HANDLE;
    fake_fs_node_t *n = &f->fs_nodes[h->node];
    uint32_t remaining = n->size > h->pos ? n->size - h->pos : 0u;
    uint32_t nread = size < remaining ? size : remaining;
    if (f->fs_max_read && nread > f->fs_max_read) nread = f->fs_max_read;
    if (nread) memcpy(buffer, n->data + h->pos, nread);
    h->pos += nread;
    *out_read = nread;
    return MINI_OK;
}

static mini_result_t fake_fs_write_cb(void *ctx, minishell_backend_file_t file, const void *buffer, uint32_t size, uint32_t *out_written)
{
    fake_state_t *f = ctx;
    fake_fs_handle_t *h = fh(file);
    if (!h) return MINI_ERR_BAD_HANDLE;
    fake_fs_node_t *n = &f->fs_nodes[h->node];
    if (h->flags & MINI_FS_APPEND) h->pos = n->size;
    uint32_t nw = size;
    if (f->fs_max_write && nw > f->fs_max_write) nw = f->fs_max_write;
    if (h->pos + nw > sizeof(n->data)) return MINI_ERR_NO_SPACE;
    memcpy(n->data + h->pos, buffer, nw);
    h->pos += nw;
    if (h->pos > n->size) n->size = h->pos;
    *out_written = nw;
    return MINI_OK;
}

static mini_result_t fake_fs_seek_cb(void *ctx, minishell_backend_file_t file, int64_t offset, uint32_t origin, uint64_t *out_position)
{
    fake_state_t *f = ctx;
    fake_fs_handle_t *h = fh(file);
    if (!h) return MINI_ERR_BAD_HANDLE;
    if (f->fs_fail_seek) return MINI_ERR_IO;
    fake_fs_node_t *n = &f->fs_nodes[h->node];
    int64_t base = origin == MINI_FS_SEEK_SET ? 0 : origin == MINI_FS_SEEK_CUR ? (int64_t)h->pos : (int64_t)n->size;
    int64_t pos = base + offset;
    if (pos < 0) return MINI_ERR_INVALID;
    h->pos = (uint32_t)pos;
    *out_position = h->pos;
    return MINI_OK;
}

static mini_result_t fake_fs_sync_cb(void *ctx, minishell_backend_file_t file)
{
    fake_state_t *f = ctx;
    if (!fh(file)) return MINI_ERR_BAD_HANDLE;
    ++f->fs_sync_calls;
    return MINI_OK;
}

static mini_result_t fake_fs_stat_cb(void *ctx, const char *path, uint32_t *out_type, uint64_t *out_size)
{
    fake_state_t *f = ctx;
    snprintf(f->fs_last_path, sizeof(f->fs_last_path), "%s", path);
    int i = find_node(path);
    if (i < 0) return MINI_ERR_NOT_FOUND;
    *out_type = f->fs_nodes[i].is_dir ? MINI_FS_TYPE_DIRECTORY : MINI_FS_TYPE_FILE;
    *out_size = f->fs_nodes[i].size;
    return MINI_OK;
}

static mini_result_t fake_fs_rename_cb(void *ctx, const char *old_path, const char *new_path)
{
    (void)ctx;
    int old_i = find_node(old_path);
    if (old_i < 0) return MINI_ERR_NOT_FOUND;
    mini_result_t parent_result = require_parent_dir(new_path);
    if (parent_result != MINI_OK) return parent_result;

    int new_i = find_node(new_path);
    if (new_i >= 0) {
        if (g_fake.fs_nodes[new_i].is_dir) return MINI_ERR_IS_DIR;
        g_fake.fs_nodes[new_i].exists = false;
    }

    snprintf(g_fake.fs_nodes[old_i].path, sizeof(g_fake.fs_nodes[old_i].path), "%s", new_path);
    return MINI_OK;
}

static mini_result_t fake_fs_remove_file_cb(void *ctx, const char *path)
{
    (void)ctx;
    int i = find_node(path);
    if (i < 0) return MINI_ERR_NOT_FOUND;
    if (g_fake.fs_nodes[i].is_dir) return MINI_ERR_IS_DIR;
    g_fake.fs_nodes[i].exists = false;
    return MINI_OK;
}

static mini_result_t fake_fs_mkdir_cb(void *ctx, const char *path)
{
    (void)ctx;
    if (find_node(path) >= 0) return MINI_ERR_EXISTS;
    mini_result_t parent_result = require_parent_dir(path);
    if (parent_result != MINI_OK) return parent_result;
    int i = alloc_node();
    if (i < 0) return MINI_ERR_NO_SPACE;
    fake_fs_node_t *n = &g_fake.fs_nodes[i];
    n->exists = true;
    n->is_dir = true;
    n->size = 0u;
    snprintf(n->path, sizeof(n->path), "%s", path);
    return MINI_OK;
}

static mini_result_t fake_fs_rmdir_cb(void *ctx, const char *path)
{
    (void)ctx;
    int i = find_node(path);
    if (i < 0) return MINI_ERR_NOT_FOUND;
    if (!g_fake.fs_nodes[i].is_dir) return MINI_ERR_NOT_DIR;
    for (int j = 0; j < 16; ++j) {
        if (j != i && g_fake.fs_nodes[j].exists &&
            path_is_child_of(g_fake.fs_nodes[j].path, path)) {
            return MINI_ERR_NOT_EMPTY;
        }
    }
    g_fake.fs_nodes[i].exists = false;
    return MINI_OK;
}

static uint64_t fake_mono(void *ctx) { return ((fake_state_t *)ctx)->mono_us; }

static mini_result_t fake_sleep(void *ctx, uint32_t ms)
{
    fake_state_t *f = ctx;
    ++f->sleep_calls;
    f->mono_us += (uint64_t)ms * 1000u;
    return MINI_OK;
}

static mini_result_t fake_utc_load(void *ctx, int64_t *sec, uint32_t *ns)
{
    fake_state_t *f = ctx;
    if (!f->utc_present) return MINI_ERR_NOT_READY;
    *sec = f->utc_seconds;
    *ns = f->utc_nanoseconds;
    return MINI_OK;
}

static mini_result_t fake_utc_store(void *ctx, int64_t sec, uint32_t ns)
{
    fake_state_t *f = ctx;
    ++f->utc_store_calls;
    if (f->utc_store_fail) return MINI_ERR_IO;
    f->utc_present = true;
    f->utc_seconds = sec;
    f->utc_nanoseconds = ns;
    return MINI_OK;
}

static mini_result_t fake_default_load(void *ctx, int32_t *lat, int32_t *lon)
{
    fake_state_t *f = ctx;
    if (!f->default_present) return MINI_ERR_NOT_READY;
    *lat = f->default_lat;
    *lon = f->default_lon;
    return MINI_OK;
}

static mini_result_t fake_default_store(void *ctx, int32_t lat, int32_t lon)
{
    fake_state_t *f = ctx;
    ++f->default_store_calls;
    if (f->default_store_fail) return MINI_ERR_IO;
    f->default_present = true;
    f->default_lat = lat;
    f->default_lon = lon;
    return MINI_OK;
}

static mini_result_t fake_default_clear(void *ctx)
{
    fake_state_t *f = ctx;
    ++f->default_clear_calls;
    if (f->default_clear_fail) return MINI_ERR_IO;
    f->default_present = false;
    return MINI_OK;
}

static mini_result_t fake_display_info(void *ctx, uint32_t *cols, uint32_t *rows)
{
    fake_state_t *f = ctx;
    *cols = f->display_columns;
    *rows = f->display_rows;
    return MINI_OK;
}

static mini_result_t fake_display_clear(void *ctx)
{
    fake_state_t *f = ctx;
    for (uint32_t r = 0; r < f->display_rows && r < 8u; ++r)
        for (uint32_t c = 0; c < f->display_columns && c < 32u; ++c)
            f->display_cells[r][c] = ' ';
    ++f->display_clear_calls;
    return MINI_OK;
}

static mini_result_t fake_display_clear_at(void *ctx, uint32_t row, uint32_t col, uint32_t rows, uint32_t cols)
{
    fake_state_t *f = ctx;
    f->display_last_clear_row = row;
    f->display_last_clear_col = col;
    f->display_last_clear_rows = rows;
    f->display_last_clear_cols = cols;
    for (uint32_t r = row; r < row + rows && r < 8u; ++r)
        for (uint32_t c = col; c < col + cols && c < 32u; ++c)
            f->display_cells[r][c] = ' ';
    return MINI_OK;
}

static mini_result_t fake_display_write(void *ctx, uint32_t row, uint32_t col, const char *text, uint32_t count)
{
    fake_state_t *f = ctx;
    f->display_last_write_count = count;
    for (uint32_t i = 0; i < count && col + i < 32u && row < 8u; ++i) f->display_cells[row][col + i] = text[i];
    return MINI_OK;
}

static mini_result_t fake_display_present(void *ctx)
{
    ++((fake_state_t *)ctx)->display_present_calls;
    return MINI_OK;
}

static mini_result_t fake_input_wait(void *ctx, uint32_t timeout_ms)
{
    fake_state_t *f = ctx;
    ++f->input_wait_calls;
    if (f->input_inject_on_wait) {
        f->input_inject_on_wait = false;
        (void)minishell_services_input_submit(&f->input_injected_event);
        return MINI_OK;
    }
    if (timeout_ms == MINI_WAIT_FOREVER) return MINI_ERR_NOT_READY;
    f->mono_us += (uint64_t)timeout_ms * 1000u;
    return MINI_ERR_TIMEOUT;
}

static void fake_input_wake(void *ctx) { ++((fake_state_t *)ctx)->input_wake_calls; }

void fake_reset(void)
{
    minishell_services_configure(NULL);
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.reported_free = 100000u;
    g_fake.reported_largest = 50000u;
    g_fake.report_memory_info = true;
    g_fake.display_columns = 10u;
    g_fake.display_rows = 4u;
    fake_fs_add_dir("/");
    fake_fs_add_dir("/sd");
}

minishell_services_port_t fake_minimal_port(void)
{
    minishell_services_port_t p;
    memset(&p, 0, sizeof(p));
    p.ctx = &g_fake;
    p.system_write = fake_system_write;
    return p;
}

minishell_services_port_t fake_full_port(void)
{
    minishell_services_port_t p = fake_minimal_port();
    p.memory_alloc = fake_mem_alloc;
    p.memory_realloc = fake_mem_realloc;
    p.memory_free = fake_mem_free;
    p.memory_get_info = fake_mem_info;
    p.fs_open = fake_fs_open_cb;
    p.fs_close = fake_fs_close_cb;
    p.fs_read = fake_fs_read_cb;
    p.fs_write = fake_fs_write_cb;
    p.fs_seek = fake_fs_seek_cb;
    p.fs_sync = fake_fs_sync_cb;
    p.fs_stat = fake_fs_stat_cb;
    p.fs_rename = fake_fs_rename_cb;
    p.fs_remove_file = fake_fs_remove_file_cb;
    p.fs_mkdir = fake_fs_mkdir_cb;
    p.fs_rmdir = fake_fs_rmdir_cb;
    p.monotonic_us = fake_mono;
    p.sleep_ms = fake_sleep;
    p.time_location_capabilities = MINI_TIMELOC_CAP_UTC | MINI_TIMELOC_CAP_SET_UTC |
                                   MINI_TIMELOC_CAP_LOCATION | MINI_TIMELOC_CAP_DEFAULT_LOCATION |
                                   MINI_TIMELOC_CAP_SET_DEFAULT_LOCATION;
    p.utc_load = fake_utc_load;
    p.utc_store = fake_utc_store;
    p.default_location_load = fake_default_load;
    p.default_location_store = fake_default_store;
    p.default_location_clear = fake_default_clear;
    p.display_capabilities = MINI_DISPLAY_CAP_TEXT;
    p.display_text_get_info = fake_display_info;
    p.display_text_clear = fake_display_clear;
    p.display_text_clear_at = fake_display_clear_at;
    p.display_text_write_at = fake_display_write;
    p.display_present = fake_display_present;
    p.input_capabilities = MINI_INPUT_CAP_KEY;
    p.input_wait = fake_input_wait;
    p.input_wake = fake_input_wake;
    return p;
}
