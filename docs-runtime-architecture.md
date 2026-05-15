# Dobby Runtime Architecture

## Purpose

The runtime layer turns immediate constructor-time hooks into deferred hook tasks. This makes the project more generic and durable across native Linux and Android processes where libraries can be loaded late, symbols can appear after initialization, and executable mappings might not be safe to patch at bootstrap time.

## Goals

- Engine-independent and game-independent hook scheduling.
- Delayed first attempt for startup safety.
- Retry while prerequisites are missing, not after deterministic patch failures.
- Portable polling fallback for Linux and Android.
- Explicit queue shutdown and pending-count visibility.
- Symbol, offset, and direct-address hook modes.

## Public Runtime API

- `DobbyEnableRuntime`
- `DobbyDisableRuntime`
- `DobbyAutoHook`
- `DobbyAutoHookSymbol`
- `DobbyWaitAndHook`
- `DobbyWaitAndHookOffset`
- `DobbyAutoHookPendingCount`

## Runtime Flow

```text
Register request
  -> wait for optional start_delay_ms
  -> wait for module / symbol / executable mapping
  -> install inline or PLT backend
  -> mark installed / failed / timed out / cancelled
  -> purge completed task from queue
```

## Safety Policy

The runtime does not implement stealth injection, anti-cheat bypass, root hiding, debugger hiding, or process-hiding behavior. Its purpose is deterministic hook lifecycle management inside authorized instrumentation, testing, debugging, observability, and compatibility workflows.

## Implementation Notes

- `UniversalHookManager` is lazy-started by `DobbyEnableRuntime()` or the first scheduled hook.
- `DobbyAutoHookSymbol()` uses default runtime options when provided, otherwise falls back to `250ms` retry and delayed first attempt.
- `DobbyWaitAndHook()` and `DobbyWaitAndHookOffset()` are synchronous bounded wait helpers.
- `DobbyAutoHookPendingCount()` exposes queue pressure for diagnostics.
- Completed tasks are removed, preventing long-running processes from accumulating stale entries.
