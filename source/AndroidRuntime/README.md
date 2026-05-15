# Android Runtime Layer

This layer transforms Dobby from a raw inline hook engine into a generalized Android Hook Runtime.

Goals:
- Android-wide compatibility
- Linker monitoring
- Auto rehook
- Transaction-safe patching
- Namespace aware symbol resolution
- PAC/BTI preparation
- Multi-hook chain
- Zygisk/LSPosed friendly runtime

Architecture:
- Dobby Core: instruction relocation only
- Runtime Layer: lifecycle and scheduling
- Linker Runtime: dlopen monitor and ELF resolver
- Transaction Layer: atomic hook operations
- Scheduler: deferred hook and retry queue
