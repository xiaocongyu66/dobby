# Dobby Runtime Architecture

## Goals

- Event-driven runtime hook framework
- Deferred hook scheduler
- Linker-aware module loading
- Auto hook lifecycle
- No hardcoded engine/game dependency

## Public Runtime API

- DobbyEnableRuntime
- DobbyAutoHook
- DobbyWaitAndHook
- DobbyAutoHookSymbol

## Runtime Flow

ModuleLoaded -> SymbolResolved -> PatchSafe -> InstallHook

## Scheduler

All hooks enter runtime queue instead of immediate inline patch.

## Loader

Future loader interception:

- dlopen
- android_dlopen_ext
- __loader_dlopen

