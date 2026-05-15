# xDL AutoHook/Header Update

This package updates the universal delayed AutoHook runtime with Android xDL-first module discovery while keeping Linux/POSIX fallback behavior.

## Changes

- Android runtime module discovery now prefers `DobbyGetLibraryBase()` / xDL when `DOBBY_ANDROID_USE_XDL` is enabled.
- Android delayed-hook module scanning uses `xdl_iterate_phdr(..., XDL_FULL_PATHNAME)` before falling back to the POSIX path on non-Android builds.
- Android PLT/GOT symbol replacement traversal now uses xDL-backed program-header iteration when xDL is enabled.
- `include/dobby.h` now contains a full usage example for delayed symbol hooks, blocking hooks, offset hooks, direct-address hooks, flag behavior, parameter meanings, and return codes.
- Linux x86_64 prebuilt artifacts were refreshed after a successful local build.

## Notes

- Android NDK is not installed in this environment, so Android ABI binaries were not regenerated here.
- The xDL path is Android-only. Linux keeps the existing `dl_iterate_phdr` / `dlsym` compatible fallback path.
- This update does not add anti-detection, stealth injection, anti-debug, or anti-anti-cheat behavior.
