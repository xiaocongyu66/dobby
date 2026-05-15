# Runtime Auto Hook Refactor Notes

## Scope

This package refactors the existing Dobby runtime extension into a generic delayed hook manager. The goal is compatibility and lifecycle reliability, not targeting any specific game, game engine, renderer, or protected environment.

## Main Changes

- Fixed `include/dobby.h` so Runtime Auto Hook declarations stay inside the header include guard and `extern "C"` block.
- Implemented the previously declared runtime C APIs:
  - `DobbyEnableRuntime`
  - `DobbyDisableRuntime`
  - `DobbyAutoHook`
  - `DobbyAutoHookSymbol`
  - `DobbyWaitAndHook`
  - `DobbyWaitAndHookOffset`
  - `DobbyAutoHookPendingCount`
- Reworked `UniversalHookManager` into a lazy-starting worker with:
  - configurable first delay;
  - configurable retry interval;
  - optional timeout;
  - callback status reporting;
  - symbol, offset, and direct-address requests;
  - queue cleanup after installed, failed, timed-out, or cancelled states.
- Changed retry semantics so the manager retries missing prerequisites only. Once a concrete target is found and patching deterministically fails, the task is marked failed instead of retrying forever.
- Updated Linux build prebuilt outputs in `prebuilt/linux-x86_64/`.
- Raised CMake minimum version from 3.5 to 3.10 to remove the deprecation warning on current CMake versions.
- Added runtime documentation:
  - `docs/auto_hook_runtime.md`
  - `docs-runtime-architecture.md`

## Validation Performed

- Rebuilt Linux shared and static libraries using `scripts/build_linux.sh`.
- Confirmed exported symbols for the runtime APIs in `prebuilt/linux-x86_64/libdobby.so`.
- Built and ran a local delayed-hook smoke test against a library loaded after scheduling. The hook installed after the target library became available and the pending queue returned to zero.

## Android Notes

The Android build script remains available in `scripts/build_android.sh`, but this environment does not provide an Android NDK, so Android binaries were not regenerated here. The runtime code is guarded for Linux/Android and uses the existing Dobby Android/ELF APIs where applicable.
