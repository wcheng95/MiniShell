# Keyer application

Keyer is the first field-oriented MiniShell application planned to run as an external Cardputer ADV ELF.

Current implementation status:

```text
K0 architecture gate      COMPLETE
K1 ADV runtime ELF        COMPLETE
K2 MiniShell Digital I/O  COMPLETE
K3 portable keyer engine  COMPLETE
K4 GPIO KeyIn/KeyOut      IN PROGRESS
```

Current module structure:

```text
main/keyer_main.c
include/keyer_types.h
src/app_controller/
src/config_service/
src/keyer_engine/
src/keyin/
src/keyout/
```

Dependency direction:

```text
config_service
      |
      v
app_controller
   /      |       \
  v       v        v
keyin  keyer_engine keyout
  |                  |
  v                  v
MiniShell Digital I/O
```

There is no sibling-module orchestration. `keyer_engine` remains MiniShell-independent.

K4 KeyIn modes:

```text
Paddle
PaddleR
SK-T
SK-R
```

K4 KeyOut modes:

```text
Paddle
PaddleR
SK
SK-M
Off
```

Default ADV deployment settings:

```text
G13/G15   active-low pull-up KeyIn
G3/G6     active-low open-drain KeyOut
20 WPM
IambicA
KeyIn=Paddle
KeyOut=SK
```

KeyOut opens in the released state and releases both lines before close/application exit. `SK-M` retains Mini-CW's special ring-grounded idle while the app is running, but shutdown still releases both outputs.

Host regression coverage:

```text
tests/keyer_engine_k3_test.c
tests/keyer_k4_io_test.c
```

The K4 ADV ELF build project is:

```text
platform/adv/elf_apps/keyer/
```

See `docs/keyer/README.md` for the staged plan and `platform/adv/elf_apps/keyer/README.md` for the K4 hardware test.
