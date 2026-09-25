#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "shell_editor.h"

static shell_editor_t e;
static void type(const char *s)
{
    while (*s) shell_editor_edit(&e, SHELL_EDIT_CHAR, (unsigned char)*s++);
}
static void submit(const char *s)
{
    shell_editor_begin(&e); type(s); shell_editor_remember(&e);
}
static void line(const char *s) { assert(!strcmp(e.line, s)); }
static void edit(shell_edit_action_t action) { shell_editor_edit(&e, action, 0); }
int main(void)
{
    shell_editor_init(&e);
    edit(SHELL_EDIT_PREVIOUS); edit(SHELL_EDIT_NEXT); line("");
    submit(""); submit("   "); assert(e.count == 0);
    strcpy(e.line, "\t \r\n"); shell_editor_remember(&e); assert(e.count == 0);
    submit("one"); shell_editor_begin(&e);
    edit(SHELL_EDIT_PREVIOUS); line("one");
    edit(SHELL_EDIT_PREVIOUS); line("one");
    edit(SHELL_EDIT_NEXT); line(""); edit(SHELL_EDIT_NEXT); line("");
    type("draft"); edit(SHELL_EDIT_LEFT);
    edit(SHELL_EDIT_PREVIOUS); line("one"); type("!"); line("one!");
    edit(SHELL_EDIT_NEXT); line("draft"); assert(e.cursor == 4);
    edit(SHELL_EDIT_PREVIOUS); line("one");
    type("!"); shell_editor_remember(&e);
    shell_editor_begin(&e); edit(SHELL_EDIT_PREVIOUS); line("one!");
    edit(SHELL_EDIT_PREVIOUS); line("one");
    submit("one"); assert(e.count == 3); /* Duplicates retained. */
    shell_editor_init(&e);
    char number[8];
    for (unsigned i = 0; i < 11; ++i) { snprintf(number, sizeof(number), "%u", i); submit(number); }
    assert(e.count == 10);
    shell_editor_begin(&e);
    for (unsigned i = 10; i != 0; --i) {
        edit(SHELL_EDIT_PREVIOUS); snprintf(number, sizeof(number), "%u", i); line(number);
    }
    edit(SHELL_EDIT_PREVIOUS); line("1");
    for (unsigned i = 2; i <= 10; ++i) {
        edit(SHELL_EDIT_NEXT); snprintf(number, sizeof(number), "%u", i); line(number);
    }
    edit(SHELL_EDIT_NEXT); line("");
    type("ac"); edit(SHELL_EDIT_LEFT); type("b"); line("abc"); assert(e.cursor == 2);
    edit(SHELL_EDIT_BACKSPACE); line("ac"); edit(SHELL_EDIT_DELETE); line("a");
    edit(SHELL_EDIT_END); edit(SHELL_EDIT_DELETE); edit(SHELL_EDIT_RIGHT); line("a");
    edit(SHELL_EDIT_HOME); edit(SHELL_EDIT_LEFT); edit(SHELL_EDIT_BACKSPACE); line("a");
    edit(SHELL_EDIT_DELETE); line("");
    for (unsigned i = 0; i < 300; ++i) type("x");
    assert(e.length == 255 && e.cursor == 255 && e.line[255] == 0);
    edit(SHELL_EDIT_HOME); type("z"); assert(e.line[0] == 'x');
    edit(SHELL_EDIT_DELETE); type("z"); assert(e.length == 255 && e.line[0] == 'z');
    shell_editor_remember(&e); shell_editor_begin(&e); edit(SHELL_EDIT_PREVIOUS);
    assert(e.length == 255 && e.line[0] == 'z');
    shell_editor_init(&e); assert(e.count == 0); line("");
    puts("shell editor/history: PASS");
}
