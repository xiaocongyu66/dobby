## Dobby

[![Contact me Telegram](https://img.shields.io/badge/Contact%20me-Telegram-blue.svg)](https://t.me/IOFramebuffer) [![Join group Telegram](https://img.shields.io/badge/Join%20group-Telegram-brightgreen.svg)](https://t.me/dobby_group)

Dobby is a lightweight, modular hook framework. This maintained fork currently supports Linux and Android builds only.

- Minimal and modular library
- Platform support in this fork: Linux and Android
- Architecture support: X86, X86-64, ARM, ARM64

## Safety and optional sources

The default build intentionally excludes `source/stealth/`. Those files are kept as archived/experimental material and are not part of the supported public API or CI surface.


## Hook lifecycle API

The legacy API remains supported:

```c
void *origin = NULL;
DobbyHook(target, replacement, &origin);
DobbyDestroy(target);
```

New code should prefer the handle-based API so hook ownership and unhook semantics are explicit:

```c
DobbyHookHandle *handle = NULL;
void *origin = NULL;
if (DobbyHookEx(target, replacement, &origin, &handle) == 0) {
  DobbyUnhook(handle);
  DobbyDestroyHandle(handle);
}
```

`DobbyUnhook()` disables the hook and keeps the handle queryable as inactive. `DobbyDestroyHandle()` releases the handle. Calling legacy `DobbyDestroy(target)` also invalidates any outstanding handle for that target.

## Compile

[docs/compile.md](docs/compile.md)

## Download

[download latest library](https://github.com/jmpews/Dobby/releases/tag/latest)

## Credits

1. [frida-gum](https://github.com/frida/frida-gum)
2. [minhook](https://github.com/TsudaKageyu/minhook)
3. [substrate](https://github.com/jevinskie/substrate).
4. [v8](https://github.com/v8/v8)
5. [dart](https://github.com/dart-lang/sdk)
6. [vixl](https://git.linaro.org/arm/vixl.git)
