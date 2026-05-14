# Dobby fixed package

This package contains source fixes applied to the uploaded `Dobby-master` archive.

## Build fixes applied

- Replaced stale `RuntimeModule::load_address` usage with `RuntimeModule::base` in Linux runtime and ELF symbol resolution code.
- Fixed Linux memory region comparator usage from field-style access to `MemRange::start()`.
- Changed `code-patch-tool-posix.cc` include from missing `core/arch/Cpu.h` to existing `core/arch/CpuRegister.h`.
- Split `MemoryPermission` and `OSMemory` declarations into `source/PlatformUnifiedInterface/platform_memory.h` to break the `platform.h` / `os_arch_features.h` circular include.
- Added `-x assembler-with-cpp` for `.asm` bridge files so GCC/Clang produce real object files instead of ignoring `.asm` as a linker input.
- Made `dobby_set_near_trampoline` a real exported symbol instead of a hidden inline function, fixing the example link error.
- Added `<sys/time.h>` to logging on Linux/Apple platforms.
- Updated `scripts/platform_builder.py` defaults for `--cmake_dir` / `--llvm_dir` and Linux arch name `aarch64`.

## Local verification

Verified on Linux x86_64 with GCC 14.2 / CMake 3.31:

```sh
cmake -S . -B build -DDOBBY_BUILD_EXAMPLE=OFF -DDOBBY_BUILD_TEST=OFF
cmake --build build -j4
```

Result: `dobby` and `dobby_static` built successfully.

Also verified examples:

```sh
cmake -S . -B build-example -DDOBBY_BUILD_EXAMPLE=ON -DDOBBY_BUILD_TEST=OFF
cmake --build build-example -j4
```

Result: `socket_example` and `socket_example_lib` built successfully.

The test target was not fully configured in this container because `capstone` is not installed (`pkg-config` could not find `capstone`).

## Included prebuilt artifacts

- `prebuilt/linux-x86_64/libdobby.a`
- `prebuilt/linux-x86_64/libdobby.so`
- `prebuilt/linux-x86_64/socket_example`

## Linux + Android xDL update

This revision focuses on Linux and Android builds.

- Vendored the supplied `xdl.zip` source into `third_party/xdl`.
- Added Android-only CMake option `DOBBY_ANDROID_USE_XDL` (default `ON`).
- On Android, enhanced `DobbySymbolResolver()` to use xDL for already-loaded ELF modules, querying both `.dynsym` and `.symtab` before falling back to Dobby's older ELF mmap resolver.
- Added `DobbyHookBySymbol(image_name, symbol_name, fake_func, out_origin_func)`, a convenience wrapper around `DobbySymbolResolver()` + `DobbyHook()`.
- Added `scripts/build_linux.sh` and `scripts/build_android.sh`.
- Added `docs/android-xdl.md` with Android build and API usage notes.
- Kept Linux and Android enabled; iOS/Windows sources remain present but were not the focus of this package.

Additional verification in this container:

```sh
./scripts/build_linux.sh
```

Result: Linux `libdobby.so`, `libdobby.a`, and `socket_example` built successfully.

```sh
# syntax-only check with a stub android/api-level.h because this container has no Android NDK
cc -fsyntax-only ... third_party/xdl/*.c
c++ -fsyntax-only ... builtin-plugin/SymbolResolver/elf/dobby_xdl_symbol_resolver.cc
```

Result: xDL Android integration sources passed syntax checks. A real Android ABI build requires Android NDK, which is not installed in this container.
## v5: Android 32/64-bit ABI packaging

- Android builds now package both `arm64-v8a` and `armeabi-v7a` by default.
- Restored ARM32 compatibility shims for legacy Dobby ARM assembler/relocator code.
- Added `source/core/arch/Cpu.h` wrapper for old ARM/x86 includes.
- Kept Windows/iOS/macOS code paths removed; this fork targets Linux + Android only.
