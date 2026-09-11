# Keyer application

Keyer is the first field-oriented MiniShell application planned to run as an external Cardputer ADV ELF.

Current implementation status:

```text
K0 architecture gate      COMPLETE
K1 ADV runtime ELF        COMPLETE
K2 MiniShell Digital I/O  COMPLETE
K3 portable keyer engine  IN PROGRESS
```

K3 deliberately contains only portable CW behavior. It has no MiniShell, GPIO, ESP-IDF, audio, display, filesystem, or board dependency.

Current module:

```text
src/keyer_engine/
    keyer_engine.c/.h   paddle / straight-key timing state machine
    keyer_decoder.c/.h  private Morse pattern decoder
```

Later stages place platform-facing code around this engine:

```text
KeyIn -> keyer_engine -> logical key state -> KeyOut / sidetone
```

See `docs/keyer/README.md` for the complete staged plan.
