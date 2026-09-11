# Keyer application

Keyer is the first field-oriented MiniShell application planned to run as an external Cardputer ADV ELF.

Current implementation status:

```text
K0 architecture gate      COMPLETE
K1 ADV runtime ELF        COMPLETE
K2 MiniShell Digital I/O  COMPLETE
K3 portable keyer engine  COMPLETE
K4 GPIO KeyIn/KeyOut      NEXT
```

K3 deliberately contains only portable CW behavior. It has no MiniShell, GPIO, ESP-IDF, audio, display, filesystem, or board dependency.

Current module:

```text
src/keyer_engine/
    keyer_engine.c/.h   paddle / straight-key timing state machine
    keyer_decoder.c/.h  private Morse pattern decoder
```

Implemented K3 behavior includes:

```text
5-60 WPM timing
1:3 dit/dah timing and 1-unit element gap
logical key_down/key_up state
opposite-paddle memory
held-squeeze alternation
Mini-CW-compatible Iambic A/B behavior
Bug mode: automatic dit + manual held dah
straight-key dit/dah classification
3-unit character and 7-unit word timing
Morse decode with Mini-CW special input gestures
invalid-Morse '~' marker
```

Host regression coverage lives in `tests/keyer_engine_k3_test.c` and is part of the strict unit suite.

Later stages place platform-facing code around this engine:

```text
KeyIn -> keyer_engine -> logical key state -> KeyOut / sidetone
```

See `docs/keyer/README.md` for the complete staged plan.
