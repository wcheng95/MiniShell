#include "shell_completion.h"
#include "minishell/api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *names[16];
static unsigned opens, closes, reads, fail_read;
static bool fail_open, fail_close;
static const char *expected_parent;
static mini_result_t open_dir(const char *path, mini_dir_t *out)
{
    ++opens;
    assert(!strcmp(path, expected_parent));
    if (fail_open) return MINI_ERR_IO;
    *out = 1; return MINI_OK;
}
static mini_result_t read_dir(mini_dir_t dir, mini_fs_dir_entry_t *entry, uint32_t *has)
{
    assert(dir == 1 && entry->struct_size == sizeof(*entry));
    if (++reads == fail_read) return MINI_ERR_IO;
    *has = names[reads - 1u] != NULL;
    if (*has) {
        strcpy(entry->name, names[reads - 1u]);
        entry->type = reads % 2u ? MINI_FS_TYPE_DIRECTORY : MINI_FS_TYPE_FILE;
    }
    return MINI_OK;
}
static mini_result_t close_dir(mini_dir_t dir)
{
    assert(dir == 1); ++closes;
    return fail_close ? MINI_ERR_IO : MINI_OK;
}
static const mini_fs_api_t fs = {
    .dir_open = open_dir, .dir_read = read_dir, .dir_close = close_dir
};
static const mini_api_t api = {.fs = &fs};
const mini_api_t *mini_api_get(void) { return &api; }

static void check(const char *line, size_t cursor, const char *parent, const char *result)
{
    shell_editor_t e, before;
    shell_editor_init(&e);
    strcpy(e.line, line); e.length = strlen(line); e.cursor = cursor;
    before = e;
    opens = closes = reads = 0u; expected_parent = parent;
    bool changed = shell_completion_expand(&e);
    assert(changed == (strcmp(line, result) != 0));
    assert(!strcmp(e.line, result) && e.length == strlen(result));
    assert(e.cursor == cursor + strlen(result) - strlen(line));
    if (!changed) assert(!memcmp(&before, &e, sizeof(e)));
    assert(opens == (parent != NULL));
    assert(closes == (parent != NULL && !fail_open));
    assert(!memcmp(e.history, before.history, sizeof(e.history)));
}
#define CHECK(line, parent, result) check(line, sizeof(line)-1u, parent, result)
int main(void)
{
    names[0] = "flash"; names[1] = "sd";
    CHECK("cd /f", "/", "cd /flash");
    names[0] = "ft8"; names[1] = NULL;
    CHECK("cd /flash/f", "/flash", "cd /flash/ft8");
    names[0] = "setting.txt";
    CHECK("cat se", ".", "cat setting.txt");
    CHECK(" \tcat\tse", ".", " \tcat\tsetting.txt");
    CHECK("app ./se", ".", "app ./setting.txt");
    CHECK("app ../se", "..", "app ../setting.txt");
    CHECK("app dir/se", "dir", "app dir/setting.txt");
    CHECK("app /se", "/", "app /setting.txt");
    check("cat se next", 6, ".", "cat setting.txt next");
    check("cat seZ", 6, NULL, "cat seZ");
    CHECK("cat /", NULL, "cat /");
    CHECK("cat ./", NULL, "cat ./");
    CHECK("cat ", NULL, "cat ");
    CHECK("", NULL, "");
    CHECK("se", NULL, "se");
    CHECK("/se", NULL, "/se");
    CHECK("ft8 se", NULL, "ft8 se");
    CHECK("run se", NULL, "run se");
    CHECK("unknown se", NULL, "unknown se");
    CHECK("c se", NULL, "c se");
    CHECK("c ./se", ".", "c ./setting.txt");
    const char *commands[] = {"cd", "ls", "cat", "df", "nano", "mkdir", "rm", "rmdir", "cp", "mv"};
    for (unsigned i = 0; i < sizeof(commands)/sizeof(commands[0]); ++i) {
        char line[64], result[64];
        snprintf(line, sizeof(line), "%s se", commands[i]);
        snprintf(result, sizeof(result), "%s setting.txt", commands[i]);
        check(line, strlen(line), ".", result);
        snprintf(line, sizeof(line), "%s other se", commands[i]);
        snprintf(result, sizeof(result), "%s other setting.txt", commands[i]);
        check(line, strlen(line), i >= 8 ? "." : NULL, i >= 8 ? result : line);
    }
    CHECK("cp one two se", NULL, "cp one two se");
    CHECK("mv one two se", NULL, "mv one two se");
    names[0] = "RT260925.txt"; names[1] = "RT260926.txt"; names[2] = "RxTxLog.txt";
    CHECK("cat RT", ".", "cat RT26092");
    names[0] = "RxTxLog.txt"; names[2] = "RT260925.txt";
    CHECK("cat RT", ".", "cat RT26092");
    CHECK("cat R", ".", "cat R");
    CHECK("cat rt", ".", "cat rt");
    names[0] = "foo"; names[1] = "foobar"; names[2] = NULL;
    CHECK("cat f", ".", "cat foo");
    CHECK("cat foo", ".", "cat foo");
    names[0] = "."; names[1] = ".."; names[2] = ".hidden"; names[3] = "hello";
    CHECK("cat .", ".", "cat .hidden");
    CHECK("cat h", ".", "cat hello");
    names[0] = "hello world"; names[1] = "hello\tworld"; names[2] = "hello\rworld";
    names[3] = "hello\nworld"; names[4] = "help";
    CHECK("cat he", ".", "cat help");
    memset(names, 0, sizeof(names)); names[0] = "setting.txt";
    fail_open = true; CHECK("cat se", ".", "cat se"); fail_open = false;
    fail_read = 1; CHECK("cat se", ".", "cat se");
    fail_read = 2; CHECK("cat se", ".", "cat se"); fail_read = 0;
    fail_close = true; CHECK("cat se", ".", "cat se"); fail_close = false;
    char long_name[256]; memset(long_name, 's', 255); long_name[255] = 0;
    names[0] = long_name;
    CHECK("cat s", ".", "cat s"); // Entire suffix must fit.
    long_name[251] = 0;
    char full[256] = "cat "; strcat(full, long_name);
    check("cat s", 5, ".", full); // Exactly 255 payload bytes fits.
    names[0] = "setting.txt";
    shell_editor_t e;
    shell_editor_init(&e); strcpy(e.line, "cat se"); e.cursor = e.length = 6;
    shell_completion_pending_t pending = {0};
    opens = closes = reads = 0; expected_parent = ".";
    assert(shell_completion_timeout_ms(&pending, 0) == -1);
    shell_completion_defer(&pending, 1000);
    assert(shell_completion_timeout_ms(&pending, 1000) == 25);
    assert(!shell_completion_poll(&pending, &e, 25999) && opens == 0);
    shell_completion_defer(&pending, 25999); // Another byte refreshes the deadline.
    assert(!shell_completion_poll(&pending, &e, 26000) && opens == 0);
    assert(shell_completion_poll(&pending, &e, 50999));
    assert(!strcmp(e.line, "cat setting.txt") && opens == 1 && closes == 1);
    assert(!shell_completion_poll(&pending, &e, 100000) && opens == 1);
    shell_completion_defer(&pending, 100000);
    pending.pending = false; // Non-printable input cancels pending work.
    assert(!shell_completion_poll(&pending, &e, 200000) && opens == 1);
    puts("shell pathname expansion: PASS");
}
