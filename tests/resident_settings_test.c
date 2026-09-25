#include "resident_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static minishell_resident_settings_t settings;
static char input[MINISHELL_SETTINGS_CAP + 2];
static size_t length, position, chunk;
static unsigned opens, closes, reads, fail_read;
static mini_result_t open_result, close_result;
static bool invalid_count;

static void defaults(void) { assert(!settings.startup[0]); }
static void parse(const char *text)
{ minishell_resident_settings_parse(text, strlen(text), &settings); }
static mini_result_t open_file(const char *path, uint32_t flags, mini_file_t *file)
{
    assert(!strcmp(path, "/flash/minishell/setting.txt") && flags == MINI_FS_READ);
    ++opens; *file = 7; return open_result;
}
static mini_result_t read_file(mini_file_t file, void *data, uint32_t size, uint32_t *count)
{
    assert(file == 7 && !closes && size && size <= MINISHELL_SETTINGS_CAP);
    if (++reads == fail_read) return MINI_ERR_IO;
    if (invalid_count) { *count = size + 1; return MINI_OK; }
    size_t n = length - position;
    if (n > size) n = size;
    if (n > chunk) n = chunk;
    memcpy(data, input + position, n); position += n; *count = (uint32_t)n;
    return MINI_OK;
}
static mini_result_t close_file(mini_file_t file)
{ assert(file == 7 && !closes); ++closes; return close_result; }
static const mini_fs_api_t fs = {.open=open_file, .read=read_file, .close=close_file};
static void reset(void)
{
    strcpy(input, "startup=first;second\n"); length = strlen(input);
    position = opens = closes = reads = fail_read = 0; chunk = MINISHELL_SETTINGS_CAP;
    open_result = close_result = MINI_OK; invalid_count = false;
    memset(&settings, 0x55, sizeof(settings));
}
static void load(bool expected)
{
    assert(minishell_resident_settings_load(&fs, &settings) == expected);
    assert(opens == 1 && closes == (open_result == MINI_OK ? 1u : 0u));
    if (!expected) defaults();
}
int main(void)
{
    parse("SSID=MiniShell\r\nPW=a=b=cdef\r\nunknown=x\n #startup=ignored\nbrightness=50\r\nstartup= d ;b");
    assert(!strcmp(settings.startup, " d ;b"));
    parse("startup=first\nstartup=last=a=b");
    assert(!strcmp(settings.startup, "last=a=b"));
    parse("startup=one\nstartup=\n"); assert(!settings.startup[0]);
    /* Retired settings are ordinary unknown keys, even with invalid values. */
    parse("brightness=50\nbrightness=invalid"); defaults();
    parse("startup=kept\nbrightness=100"); assert(!strcmp(settings.startup, "kept"));
    parse("Startup=one\n startup=two\nstartup =three"); defaults();
    const char nul[] = "startup=one\nstartup=two\0hidden";
    minishell_resident_settings_parse(nul, sizeof(nul)-1, &settings);
    assert(!strcmp(settings.startup, "one"));
    minishell_resident_settings_parse(NULL, 0, &settings); defaults();
    reset(); memcpy(input, "startup=", 8); memset(input+8, 'a', MINISHELL_SETTINGS_CAP-8);
    minishell_resident_settings_parse(input, MINISHELL_SETTINGS_CAP, &settings);
    assert(strlen(settings.startup) == MINISHELL_SETTINGS_CAP-8);
    minishell_resident_settings_parse(input, MINISHELL_SETTINGS_CAP+1, &settings); defaults();
    for (unsigned size = 1; size <= 32; ++size) {
        reset(); chunk = size; load(true);
        assert(!strcmp(settings.startup, "first;second"));
    }
    reset(); chunk = 3; load(true); unsigned calls = reads;
    for (unsigned i = 1; i <= calls; ++i) { reset(); chunk = 3; fail_read = i; load(false); }
    reset(); open_result = MINI_ERR_NOT_FOUND; load(false); assert(!reads);
    reset(); open_result = MINI_ERR_IO; load(false);
    reset(); close_result = MINI_ERR_IO; load(false);
    reset(); invalid_count = true; load(false);
    reset(); length = 0; load(true); defaults();
    for (unsigned size = MINISHELL_SETTINGS_CAP-1; size <= MINISHELL_SETTINGS_CAP+1; ++size) {
        reset(); input[length++] = '#'; memset(input+length, 'x', size-length); length = size;
        load(size <= MINISHELL_SETTINGS_CAP);
    }
    reset(); memset(input+length, '#', MINISHELL_SETTINGS_CAP-length); length = MINISHELL_SETTINGS_CAP;
    fail_read = 2; load(false); /* Failed exact-limit EOF probe. */
    reset(); assert(!minishell_resident_settings_load(NULL, &settings)); defaults();
    puts("resident settings: PASS");
    return 0;
}
