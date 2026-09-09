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

If a foreground full-screen application previously owned Display, the first Console output returns the physical display to resident-console presentation. The following `M$>` prompt then continues below the utility output rather than clearing the screen back to row 0.

A later ADV enhancement may retain about 50 lines of resident-console history and scroll the 7-row viewport. That history remains private resident-console behavior; applications still write only to Console.

## Ownership rule

Applications must choose the output surface according to intent:

```text
normal utility result / usage / user error     Console
interactive full-screen UI                     Display
internal diagnostic / probe / debug message    System
```

Do not mirror System diagnostics into Display merely to make them visible on embedded hardware. Do not make command-style utilities fake full-screen Display applications merely to show text.
