# Android API enhancement patch

This package adds a safe Android-oriented hook convenience layer to Dobby.

## Added

- `source/dobby_android_api.cpp`
- `DobbyAndroid*` APIs in `include/dobby.h`
- hook records with `DobbyAndroidListHooks`
- duplicate hook protection
- symbol, function pointer, and library-offset hook helpers
- `DobbyAndroidGetLastError` and status names
- Android usage documentation in `docs/android_hook_api.md`
- simple Android example in `examples/android_symbol_hook_example.c`

## Safety boundary

This patch does not add stealth injection, anti-detection, anti-debug bypass, anti-cheat bypass, map hiding, integrity-check bypass, or module hiding.


Note: `source/stealth/` is not included in the default build and is treated as archived/experimental material. The supported Android API layer remains limited to ordinary hook management, symbol resolution, and hook bookkeeping.
