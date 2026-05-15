# Dobby

[![Contact me Telegram](https://img.shields.io/badge/Contact%20me-Telegram-blue.svg)](https://t.me/IOFramebuffer) [![Join group Telegram](https://img.shields.io/badge/Join%20group-Telegram-brightgreen.svg)](https://t.me/dobby_group)

Dobby is a lightweight, modular hook framework. This maintained fork currently supports Linux and Android builds only.

- Minimal and modular library
- Platform support in this fork: Linux and Android
- Architecture support: X86, X86-64, ARM, ARM64
- Generic runtime delayed hook scheduling that is not tied to a specific game, engine, renderer, or framework

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

## Universal Auto Hook Runtime

The runtime API defers hook installation until the target module, symbol, offset target, or executable mapping is actually ready. This avoids patching too early during constructors, early process attach, or asynchronous library loading.

```c
static void *orig_target = NULL;

static int replacement_target(int value) {
  typedef int (*target_fn_t)(int);
  target_fn_t origin = (target_fn_t)orig_target;
  return origin ? origin(value) : value;
}

__attribute__((constructor))
static void init_runtime_hooks(void) {
  DobbyRuntimeOptions options = {0};
  options.retry_interval_ms = 250;
  options.start_delay_ms = 250;
  options.timeout_ms = 10000;
  options.flags = DOBBY_AUTOHOOK_WAIT_MODULE |
                  DOBBY_AUTOHOOK_WAIT_SYMBOL |
                  DOBBY_AUTOHOOK_RETRY |
                  DOBBY_AUTOHOOK_DELAY_FIRST;
  DobbyEnableRuntime(&options);

  DobbyAutoHookSymbol("libtarget.so", "target_func", (void *)replacement_target, &orig_target);
}
```

See [docs/auto_hook_runtime.md](docs/auto_hook_runtime.md) and [docs-runtime-architecture.md](docs-runtime-architecture.md).

## Compile

[docs/compile.md](docs/compile.md)

For Linux:

```bash
bash scripts/build_linux.sh
```

For Android, set `ANDROID_NDK_HOME` or `ANDROID_NDK_ROOT`, then run:

```bash
bash scripts/build_android.sh
```

## Download

[download latest library](https://github.com/jmpews/Dobby/releases/tag/latest)

## Credits

1. [frida-gum](https://github.com/frida/frida-gum)
2. [minhook](https://github.com/TsudaKageyu/minhook)
3. [substrate](https://github.com/jevinskie/substrate)
4. [v8](https://github.com/v8/v8)
5. [dart](https://github.com/dart-lang/sdk)
6. [vixl](https://git.linaro.org/arm/vixl.git)
