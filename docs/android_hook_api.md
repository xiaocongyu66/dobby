# Android hook API layer

This fork adds an Android-oriented convenience layer on top of the existing Dobby inline hook core. The goal is to make legitimate Android native instrumentation easier to write and easier to audit.

The API is intentionally visible and inspectable. It does not provide stealth loading, anti-detection, anti-debug bypasses, anti-cheat bypasses, or process hiding. Use it for self-owned apps, offline demos, security labs, regression tests, and debugging.

## Design goals

- Substrate-style ergonomics without copying Substrate implementation code.
- `so + symbol` hooks for JNI/native functions exported from shared objects.
- `so + offset` hooks for functions known by static analysis in an offline lab.
- Hook lifecycle tracking so tests can list, check, and remove hooks.
- Clear error codes and last-error strings for demo harnesses and CI logs.
- Android-first behavior with xDL when available, while still compiling on Linux for smoke tests.

## Public API

```c
uintptr_t DobbyAndroidGetModuleBase(const char *image_name);
void *DobbyAndroidFindSymbol(const char *image_name, const char *symbol_name);

int DobbyAndroidHookFunction(void *target, void *replace, void **origin);
int DobbyAndroidHookSymbol(const char *image_name, const char *symbol_name, void *replace, void **origin);
int DobbyAndroidHookOffset(const char *image_name, uintptr_t offset, void *replace, void **origin);
int DobbyAndroidUnhook(void *target);

int DobbyAndroidIsHooked(void *target);
int DobbyAndroidListHooks(DobbyAndroidHookRecord *records, int max_count);
int DobbyAndroidClearAllHooks();

const char *DobbyAndroidStatusName(int code);
const char *DobbyAndroidGetLastError();
```

## Error codes

```c
DOBBY_ANDROID_OK = 0
DOBBY_ANDROID_ERR_INVALID_ARGUMENT = -1
DOBBY_ANDROID_ERR_LIBRARY_NOT_FOUND = -2
DOBBY_ANDROID_ERR_SYMBOL_NOT_FOUND = -3
DOBBY_ANDROID_ERR_ALREADY_HOOKED = -4
DOBBY_ANDROID_ERR_HOOK_FAILED = -5
DOBBY_ANDROID_ERR_UNHOOK_FAILED = -6
DOBBY_ANDROID_ERR_BUFFER_TOO_SMALL = -7
```

## Symbol hook example

```c
#include "dobby.h"
#include <android/log.h>
#include <string.h>

static int (*orig_strcmp)(const char *a, const char *b);

static int fake_strcmp(const char *a, const char *b) {
  __android_log_print(ANDROID_LOG_INFO, "demo", "strcmp(%s, %s)", a, b);
  return orig_strcmp(a, b);
}

__attribute__((constructor)) static void install_demo_hook(void) {
  int rc = DobbyAndroidHookSymbol("libc.so", "strcmp", (void *)fake_strcmp, (void **)&orig_strcmp);
  if (rc != DOBBY_ANDROID_OK) {
    __android_log_print(ANDROID_LOG_ERROR, "demo", "hook failed: %s / %s",
                        DobbyAndroidStatusName(rc), DobbyAndroidGetLastError());
  }
}
```

## Offset hook example

```c
static void *orig_target = 0;

__attribute__((constructor)) static void install_offset_hook(void) {
  uintptr_t offset = 0x1234; // RVA from your own offline test library.
  int rc = DobbyAndroidHookOffset("liboffline_demo.so", offset, (void *)fake_target, &orig_target);
  if (rc != DOBBY_ANDROID_OK) {
    // Read DobbyAndroidGetLastError() for details.
  }
}
```

## Listing hooks

```c
DobbyAndroidHookRecord records[16];
int total = DobbyAndroidListHooks(records, 16);
for (int i = 0; i < total && i < 16; i++) {
  // records[i].image_name, records[i].symbol_name, records[i].target_addr, records[i].enabled
}
```

## Build

```bash
# Linux smoke build
bash scripts/build_linux.sh

# Android build; requires ANDROID_NDK_HOME or ANDROID_NDK_ROOT
bash scripts/build_android.sh arm64-v8a armeabi-v7a
```

Outputs are copied to:

```text
prebuilt/linux-x86_64/
prebuilt/android/arm64-v8a/
prebuilt/android/armeabi-v7a/
prebuilt/android/include/dobby.h
```

## Detection-demo guidance

For an offline defensive demo, pair this API with checks that report hook visibility rather than hiding it:

- list hooks with `DobbyAndroidListHooks`
- verify function prologues before and after hook install
- compare `.text` section hashes in your own demo library
- enumerate process maps and loaded libraries
- verify GOT/PLT entries in your own demo library

Do not use this layer to bypass third-party protections or conceal instrumentation.
