# MiniShell Command Roadmap

Resident shell commands remain limited to lifecycle/discovery (`help`, `status`, `apps`, `run`, direct app launch, `exit`). Portable behavior belongs in runtime apps when public MiniShell services can express it.

Audio and Control are services rather than resident commands; future diagnostics should normally be small apps.

The H1-H5 internal architecture-audit housekeeping is complete. The current runtime milestone is A0-A3/P1-P2/V1: make the resident shell/startup portable, add the ADV backend, formalize MiniFT8 profiles, and validate the public API across Linux and ADV before RX-1B resumes.

New public API work remains application-driven. Backward compatibility is not frozen yet; a formal binary ABI is deferred until independently built applications require one.
