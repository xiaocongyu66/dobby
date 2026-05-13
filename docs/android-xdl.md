# Android xDL integration

This fork keeps Linux and Android as the primary build targets and embeds the
official xDL source into Dobby for Android builds.

## What is bundled

- `third_party/xdl` is vendored from the supplied official `xDL-master` archive.
- The vendored xDL version is 2.3.0.
- Android builds compile xDL directly into `libdobby.so` / `libdobby.a` when
  `DOBBY_ANDROID_USE_XDL=ON`.
- Apps do not need to package a separate `libxdl.so`.
- Apps can include only `dobby.h`; that header exposes both Dobby APIs and the
  commonly used xDL APIs.

## API additions

### xDL APIs available through `dobby.h`

Raw official xDL symbols are declared in `dobby.h` on Android:

```c
xdl_open;
xdl_open2;
xdl_close;
xdl_sym;
xdl_dsym;
xdl_addr;
xdl_addr4;
xdl_addr_clean;
xdl_iterate_phdr;
xdl_info;
```

Dobby also provides prefixed helpers:

```c
DobbyXdlOpen;
DobbyXdlOpenFromInfo;
DobbyXdlClose;
DobbyXdlSym;
DobbyXdlAddr;
DobbyXdlInfo;
DobbyGetLibraryBase;
DobbyGetLibraryPath;
DobbyGetAddressInfo;
```

`DobbyXdlOpen()` adds a small cache for common Android system libraries, inspired
by shadowhook's xDL wrapper, while still falling back to official xDL.

### Enhanced symbol resolving / hook helpers

```c
void *DobbySymbolResolverEx(const char *image_name, const char *symbol_name,
                            uint32_t flags, size_t *symbol_size);

int DobbyHookBySymbolEx(const char *image_name, const char *symbol_name,
                        void *fake_func, void **out_origin_func,
                        uint32_t flags, size_t *symbol_size);

int DobbyHookBySymbolCallback(const char *image_name, const char *symbol_name,
                              void *fake_func, void **out_origin_func,
                              uint32_t flags,
                              dobby_hooked_callback_t hooked,
                              void *hooked_arg);

int DobbyDestroyBySymbol(const char *image_name, const char *symbol_name);
```

Flags:

```c
DOBBY_SYMBOL_RESOLVER_DEFAULT
DOBBY_SYMBOL_RESOLVER_DYNSYM_ONLY
DOBBY_SYMBOL_RESOLVER_SYMTAB_ONLY
DOBBY_SYMBOL_RESOLVER_FORCE_LOAD
DOBBY_SYMBOL_RESOLVER_FULL_PATHNAME
```

On Android, default lookup uses xDL and searches `.dynsym` first, then `.symtab`.
This follows the practical behavior used by Android inline hook frameworks such
as shadowhook and GlossHook: prefer normal dynamic symbols, then use debug/static
symbols when present.

## Example

```c
#include "dobby.h"

static int (*orig_open)(const char *, int, ...);

static int fake_open(const char *path, int flags, ...) {
  return orig_open(path, flags);
}

void install_hook(void) {
  size_t symbol_size = 0;
  DobbyHookBySymbolEx("libc.so", "open", (void *)fake_open,
                      (void **)&orig_open,
                      DOBBY_SYMBOL_RESOLVER_DEFAULT,
                      &symbol_size);
}
```

You can also use raw xDL directly from `dobby.h`:

```c
void *handle = xdl_open("libart.so", XDL_DEFAULT);
size_t size = 0;
void *addr = xdl_dsym(handle, "SomeNonExportedSymbol", &size);
xdl_close(handle);
```

## Linux build

```bash
./scripts/build_linux.sh
```

Outputs are copied to:

```text
prebuilt/linux-x86_64/
```

## Android build

Install Android NDK and Ninja, then set `ANDROID_NDK_HOME` or `ANDROID_NDK_ROOT`.

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk
./scripts/build_android.sh
```

Default ABIs are `arm64-v8a` and `armeabi-v7a`. You can build specific ABIs:

```bash
./scripts/build_android.sh arm64-v8a armeabi-v7a x86_64 x86
```

Outputs are copied to:

```text
prebuilt/android/<abi>/
prebuilt/android/include/dobby.h
```
