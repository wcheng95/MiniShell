# MiniShell Console API

Status: **current public application contract; compatibility not frozen**

## Purpose

The Console service is the user-facing, line-oriented output stream for command-style applications such as:

```text
date
ls
cat
cp
df
free
mkdir
mv
rm
rmdir
```

It is intentionally distinct from both System diagnostics and the full-screen Display service.

```text
Console.write()   user-facing shell/utility output
Display           application-owned screen/UI
System.write()    diagnostics/debugging
```

## Public contract

```c
typedef struct {
    uint32_t struct_size;
    void (*write)(const char *text);
} mini_console_api_t;
```

Applications obtain it through:

```c
const mini_api_t *api = mini_api_get();
api->console->write("text\n");
```

Console is optional at the API-table level. A command-style application requiring it must verify that `api->console` and `api->console->write` are available.

## Backend behavior

### Linux

Console output joins the normal terminal output stream. Existing command-line behavior is therefore unchanged.

### Cardputer ADV

Console output joins the resident MiniShell console stream:

```text
Console.write()
    -> ADV resident console
    -> Cardputer 20x7 text console
    -> USB Serial/JTAG mirror
```

If a foreground full-screen application previously owned Display, the first Console output restores the retained console history and appends the new output. Application Display content, colors and separators are not retained in console history. The following `M$>` prompt continues below the utility output.

ADV retains 50 physical 20-column rows in a fixed resident ring, including the
current partial row. Wrapped output consumes additional rows. The live viewport
shows the newest seven rows, top-aligned until seven rows exist.

While the resident shell reads a command, Fn+Up scrolls five rows older and
Fn+Down scrolls five rows newer, clamping at either end. Scrolling preserves the
command edit buffer and does not replay output to USB. Any new Console output
(including typed echo, Backspace, Enter and the prompt) returns to the live tail.

History survives foreground Display use but not reboot/deep sleep. System
diagnostics are excluded. This is private ADV resident-console behavior, not
part of the application Console API; applications still write only to Console.
Linux scrollback remains owned by the host terminal.

Command history is separate: the resident core retains ten nonblank submitted
interactive commands in RAM, including duplicates and unexpanded aliases.
Linux Up/Down and ADV Fn+Left/Right (Fn+`,` / Fn+`/`) navigate it. Moving past the
newest command restores the original draft; edits do not mutate stored entries.
Startup and redirected input are excluded, and a new session resets history.
Linux cursor keys/Home/End and Backspace/Delete edit recalled lines. ADV uses
ordinary typing/Backspace and forward Delete; its Fn+Up/Down scrollback bindings
are unchanged. This adds no public Console or Input capability.

During ADV editing, the prompt region is redrawn in place across 20-column rows.
The USB mirror uses an ANSI single-row tail preview (up to 74 command characters)
and prints the full submitted line before newline; use a terminal at least 80
columns wide for this preview. Linux pans long editable lines horizontally to
fit the terminal width. Redirected Linux stdin keeps its existing line reader.

## Ownership rule

Applications must choose the output surface according to intent:

```text
normal utility result / usage / user error     Console
interactive full-screen UI                     Display
internal diagnostic / probe / debug message    System
```

Do not mirror System diagnostics into Display merely to make them visible on embedded hardware. Do not make command-style utilities fake full-screen Display applications merely to show text.
