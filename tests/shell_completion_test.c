#include "shell_completion.h"
#include "minishell/api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *names[128];
static unsigned opens, closes, reads, fail_read;
static bool fail_open, fail_close;
static unsigned fail_open_pass, fail_read_pass, fail_close_pass;
static const char *expected_parent;
static mini_result_t open_dir(const char *path, mini_dir_t *out)
{
    ++opens;
    assert(!strcmp(path, expected_parent));
    if (fail_open || opens == fail_open_pass) return MINI_ERR_IO;
    reads = 0;
    *out = 1; return MINI_OK;
}
static mini_result_t read_dir(mini_dir_t dir, mini_fs_dir_entry_t *entry, uint32_t *has)
{
    assert(dir == 1 && entry->struct_size == sizeof(*entry));
    if (++reads == fail_read && (!fail_read_pass || opens == fail_read_pass)) return MINI_ERR_IO;
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
    return (fail_close || opens == fail_close_pass) ? MINI_ERR_IO : MINI_OK;
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
static char output[32768];
static size_t emitted;
static void emit(const mini_fs_dir_entry_t *entry, void *ctx)
{
    assert(ctx == output);
    strcat(output, entry->name);
    if (entry->type == MINI_FS_TYPE_DIRECTORY) strcat(output, "/");
    strcat(output, "\n");
    ++emitted;
}
static void listing(const char *line, size_t cursor, const char *parent,
                    const char *expected, unsigned expected_opens, unsigned expected_closes)
{
    shell_editor_t e, before;
    shell_editor_init(&e);
    strcpy(e.line, "old command"); shell_editor_remember(&e); shell_editor_begin(&e);
    strcpy(e.draft, "preserved draft"); e.draft_cursor = 3;
    strcpy(e.line, line); e.length = strlen(line); e.cursor = cursor;
    before = e; output[0] = 0; emitted = 0;
    opens = closes = reads = 0; expected_parent = parent;
    shell_completion_result_t result = shell_completion_tab(&e, emit, output);
    assert(result == (*expected ? SHELL_COMPLETION_LISTED : SHELL_COMPLETION_NONE));
    assert(!strcmp(output, expected) && !memcmp(&e, &before, sizeof(e)));
    assert(opens == expected_opens && closes == expected_closes);
}
#define LIST(line,parent,expected,o,c) listing(line,sizeof(line)-1u,parent,expected,o,c)
static void listing_tests(void)
{
    memset(names, 0, sizeof(names));
    names[0] = "foo"; names[1] = "foobar";
    LIST("cat absent", ".", "", 1, 1);
    LIST("cat foobar", ".", "", 1, 1);
    LIST("cat foo", ".", "foo/\nfoobar\n", 2, 2);
    LIST("unknown ./foo", ".", "foo/\nfoobar\n", 2, 2);
    LIST("cp other foo", ".", "foo/\nfoobar\n", 2, 2);
    LIST("mv foo", ".", "foo/\nfoobar\n", 2, 2);
    LIST("foo", NULL, "", 0, 0);
    LIST("unknown foo", NULL, "", 0, 0);
    listing("cat fooZ", 7, NULL, "", 0, 0);
    names[0] = "foobar"; names[1] = "foo";
    LIST("cat foo", ".", "foobar/\nfoo\n", 2, 2);
    names[0] = "."; names[1] = ".."; names[2] = ".hidden"; names[3] = ".hello";
    LIST("cat .h", ".", ".hidden/\n.hello\n", 2, 2);
    names[0] = "foo"; names[1] = "foobar"; names[2] = ".foo";
    names[3] = "foo bar"; names[4] = "foo\tbar"; names[5] = "foo\rbar";
    names[6] = "foo\nbar"; names[7] = "unrelated";
    LIST("cat foo", ".", "foo/\nfoobar\n", 2, 2);
    fail_open = true; LIST("cat foo", ".", "", 1, 0); fail_open = false;
    fail_read = 2; LIST("cat foo", ".", "", 1, 1); fail_read = 0;
    fail_close = true; LIST("cat foo", ".", "", 1, 1); fail_close = false;
    fail_open_pass = 2; LIST("cat foo", ".", "", 2, 1); fail_open_pass = 0;
    fail_read_pass = 2; fail_read = 1; LIST("cat foo", ".", "", 2, 2);
    fail_read = 2; LIST("cat foo", ".", "foo/\n", 2, 2);
    fail_read = fail_read_pass = 0;
    fail_close_pass = 2; LIST("cat foo", ".", "foo/\nfoobar\n", 2, 2); fail_close_pass = 0;
    // One Tab expands only; the callback cannot start presentation.
    shell_editor_t e; shell_editor_init(&e); strcpy(e.line,"cat f");e.length=e.cursor=5;
    output[0]=0;opens=closes=reads=0;
    assert(shell_completion_tab(&e,emit,output)==SHELL_COMPLETION_EXPANDED);
    assert(!strcmp(e.line,"cat foo") && !*output && opens==1 && closes==1);
    // A full line cannot expand, but multiple matches may still be listed.
    char full_line[256]; memset(full_line,' ',250); strcpy(full_line+250,"cat f");
    listing(full_line,255,".","foo/\nfoobar\n",2,2);
    // More choices than the ADV history capacity, streamed without a core cap.
    char many[100][16], expected[2000] = "";
    memset(names,0,sizeof(names));
    for (unsigned i=0;i<100;++i) {
        snprintf(many[i],sizeof(many[i]),"a%02u",i);names[i]=many[i];
        strcat(expected,many[i]);strcat(expected,i%2u ? "\n" : "/\n");
    }
    LIST("cat a", ".", expected, 2, 2); assert(emitted==100);
}

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
    listing_tests();
    puts("shell pathname expansion and choices: PASS");
}
