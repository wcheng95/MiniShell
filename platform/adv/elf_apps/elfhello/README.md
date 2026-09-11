# elfhello — ADV K1 runtime-ELF proof

`elfhello` is the minimal external application used to prove Cardputer ADV runtime ELF loading before Keyer depends on it.

The application source is platform-independent. It includes only the public MiniShell API and imports one resident symbol:

```text
mini_api_get()
```

After obtaining the API table, Console output is an indirect MiniShell API call.

## Build

Use the same ESP-IDF family as the ADV firmware. Espressif's runtime-ELF project target is `elf`; do not build the unrelated normal firmware image for this application:

```bash
cd ~/projects/MiniShell/platform/adv/elf_apps/elfhello
idf.py fullclean
idf.py elf
```

The Espressif ELF build produces:

```text
build/elfhello.app.elf
```

Install those exact bytes under the MiniShell application filename:

```text
elfhello.elf
```

Renaming/copying the file does not change the binary.

The K1 CI gate also verifies that the runtime ELF has exactly one unresolved resident import:

```text
mini_api_get
```

All platform services are then reached indirectly through the returned MiniShell API table.

## K1 hardware validation

First install only on SD:

```text
/sd/elfhello.elf
```

At `M$>`:

```text
apps
elfhello
```

Expected application output:

```text
elfhello: MiniShell API OK
```

The USB/debug log should show:

```text
ADV ELF: loading /sd/elfhello.elf
```

Then copy the exact same binary into internal flash using MiniShell itself:

```text
cp /sd/elfhello.elf /flash/elfhello.elf
```

With both copies present, run:

```text
elfhello
```

Expected USB/debug source line:

```text
ADV ELF: loading /flash/elfhello.elf
```

This proves external precedence:

```text
/flash/<app>.elf > /sd/<app>.elf
```

Remove the SD copy and run once more to prove flash-only operation:

```text
rm /sd/elfhello.elf
elfhello
```

Finally, compiled-in applications must remain first. Copying the test ELF under a compiled-in name must not invoke the ELF loader:

```text
cp /flash/elfhello.elf /flash/hello.elf
hello
rm /flash/hello.elf
```

There must be no `ADV ELF: loading /flash/hello.elf` diagnostic. The compiled-in `hello` application wins.

Canonical ADV resolution is therefore:

```text
1. compiled-in application
2. /flash/<app>.elf
3. /sd/<app>.elf
```

## Scope

K1 intentionally loads the complete ELF file into RAM, relocates executable/data sections using Espressif `elf_loader`, runs it on the normal MiniShell foreground application task, then deinitializes and frees the loaded image.

That is sufficient for `elfhello` and the initial Keyer work. Large-app/XIP work such as a future external MiniFT8 is a separate problem and is not part of K1.
