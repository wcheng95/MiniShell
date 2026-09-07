# MiniShell Command Roadmap

Resident shell commands remain limited to lifecycle/discovery (`help`, `status`, `apps`, `run`, direct app launch, `exit`). Portable behavior belongs in runtime apps when public MiniShell services can express it.

Audio and Control are services rather than resident commands; future diagnostics should normally be small apps.

Before expanding commands or MiniFT8 DSP/radio work:

```text
split Linux backend
remove POSIX loader semantics from portable core
split Filesystem private helpers
then evaluate stateful ANSI/CSI parser cost
```

New ABI work remains application-driven.
