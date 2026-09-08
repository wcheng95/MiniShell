# MiniShell Command Roadmap

Resident shell commands remain limited to lifecycle/discovery (`help`, `status`, `apps`, `run`, direct app launch, `exit`). Portable behavior belongs in runtime apps when public MiniShell services can express it.

Audio and Control are services rather than resident commands; future diagnostics should normally be small apps.

The H1-H3 internal housekeeping gate is complete. The remaining audit item is H5, stateful terminal handling for ANSI/CSI sequences split across reads; its cost is evaluated separately and it need not automatically block application-driven work.

New ABI work remains application-driven.
