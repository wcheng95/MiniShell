/* In-memory MiniShell FS contract fake: no test bypasses the app's private port. */
static char fs_destination[8192], fs_temporary[8192], fs_csv[8192];
static bool fs_csv_exists, fs_csv_active;
static unsigned fs_csv_opens, fs_csv_reads;
static void (*fs_before_csv)(void);
static bool fs_exists, fs_temp_exists, fs_writing, fs_live;
static unsigned fs_position, fs_writes, fs_reads, fs_commits, fs_attempts, fs_closes, fs_removes;
static unsigned fs_read_limit = 7, fs_write_limit = 11;
static const char *fs_fail;
static void (*fs_before_save)(void);
static bool fs_failure(const char *op) { return fs_fail && !strcmp(fs_fail, op); }
static mini_result_t fs_open(const char *path, uint32_t flags, mini_file_t *out)
{
    assert(!fs_live);
    fs_writing = flags != MINI_FS_READ;
    fs_csv_active = !strcmp(path, "/flash/minicw/qsocalls.csv");
    if (fs_csv_active) {
        assert(!fs_writing);
        ++fs_csv_opens;
        if (fs_before_csv) fs_before_csv();
    } else assert(!strcmp(path, fs_writing ? "/flash/minicw/setting.tmp" : "/flash/minicw/setting.txt"));
    if (fs_writing) { ++fs_attempts; assert(flags == (MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC)); }
    if (fs_failure(fs_writing ? "open_write" : "open_read")) return MINI_ERR_IO;
    if (!fs_writing && !(fs_csv_active ? fs_csv_exists : fs_exists)) return MINI_ERR_NOT_FOUND;
    if (fs_writing) { fs_temporary[0] = 0; fs_temp_exists = true; }
    fs_position = 0; fs_live = true; *out = 77; return MINI_OK;
}
static mini_result_t fs_close(mini_file_t f)
{
    assert(f == 77 && fs_live); fs_live = false; ++fs_closes;
    return fs_failure(fs_writing ? "close_write" : "close_read") ? MINI_ERR_IO : MINI_OK;
}
static mini_result_t fs_read(mini_file_t f, void *buf, uint32_t n, uint32_t *got)
{
    assert(f == 77 && fs_live && !fs_writing); ++fs_reads;
    if (fs_failure("read") || (fs_failure("read_late") && fs_reads > 1)) return MINI_ERR_IO;
    if (fs_csv_active) ++fs_csv_reads;
    const char *source = fs_csv_active ? fs_csv : fs_destination;
    size_t left = strlen(source) - fs_position;
    if (n > left) n = (uint32_t)left;
    if (n > fs_read_limit) n = fs_read_limit;
    memcpy(buf, source + fs_position, n);
    if (n && fs_failure("nul")) ((char *)buf)[0] = 0;
    fs_position += n; *got = n; return MINI_OK;
}
static mini_result_t fs_write(mini_file_t f, const void *buf, uint32_t n, uint32_t *put)
{
    assert(f == 77 && fs_live && fs_writing); ++fs_writes;
    if (fs_failure("write") || (fs_failure("write_late") && fs_writes > 1)) return MINI_ERR_IO;
    if (fs_failure("zero_write")) { *put = 0; return MINI_OK; }
    if (n > fs_write_limit) n = fs_write_limit;
    assert(fs_position + n < sizeof(fs_temporary));
    memcpy(fs_temporary + fs_position, buf, n); fs_position += n;
    fs_temporary[fs_position] = 0; *put = n; return MINI_OK;
}
static mini_result_t fs_sync(mini_file_t f)
{
    assert(f == 77 && fs_live && fs_writing);
    return fs_failure("sync") ? MINI_ERR_IO : MINI_OK;
}
static mini_result_t fs_remove(const char *path)
{
    assert(!strcmp(path, "/flash/minicw/setting.tmp")); ++fs_removes;
    if (fs_failure("remove")) return MINI_ERR_IO;
    fs_temp_exists = false; return MINI_OK;
}
static mini_result_t fs_mkdir(const char *path)
{
    if (fs_before_save) fs_before_save();
    assert(!strcmp(path, "/flash/minicw"));
    return fs_failure("mkdir") ? MINI_ERR_IO : MINI_ERR_EXISTS;
}
static mini_result_t fs_rename(const char *from, const char *to)
{
    assert(!fs_live && fs_temp_exists);
    assert(!strcmp(from, "/flash/minicw/setting.tmp") && !strcmp(to, "/flash/minicw/setting.txt"));
    if (fs_failure("rename")) return MINI_ERR_IO;
    strcpy(fs_destination, fs_temporary); fs_exists = true; fs_temp_exists = false; ++fs_commits; return MINI_OK;
}
static const mini_fs_api_t fs_api = {.struct_size = sizeof(fs_api), .open = fs_open, .close = fs_close,
    .read = fs_read, .write = fs_write, .sync = fs_sync, .remove_file = fs_remove, .mkdir = fs_mkdir, .rename = fs_rename};
static void fs_reset(void)
{
    assert(!fs_live);
    fs_exists = fs_temp_exists = false; fs_destination[0] = fs_temporary[0] = 0;
    fs_writes = fs_reads = fs_commits = fs_attempts = fs_closes = fs_removes = 0;
    fs_csv_exists = fs_csv_active = false; fs_csv[0] = 0; fs_csv_opens = fs_csv_reads = 0; fs_before_csv = NULL;
    fs_fail = NULL; fs_before_save = NULL; fs_read_limit = 7; fs_write_limit = 11;
}
