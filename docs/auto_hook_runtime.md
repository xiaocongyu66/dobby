# Universal Auto Hook Runtime

This fork adds a generic delayed hook runtime. It is intentionally not tied to a specific game, engine, renderer, framework, or Android-only loader path.

The runtime is useful when the target module or symbol is not ready during a constructor, plugin bootstrap, app startup callback, or early process attach. Instead of patching immediately, a hook request enters a small worker queue and is installed only after the module, symbol, target address, and executable mapping are present.

## Design goals

- No hardcoded game or engine assumptions.
- Works with Linux and Android native modules.
- Supports symbol, offset, and direct-address hook requests.
- Uses delayed first attempt by default to avoid constructor-order and early-loader races.
- Uses bounded retry and optional timeout so unresolved hooks do not create unbounded growth.
- Removes completed, failed, timed-out, and cancelled tasks from the queue.
- Keeps linker-event flags in the ABI, while the portable implementation falls back to polling where no stable loader callback is available.

## Public API

```c
int DobbyEnableRuntime(const DobbyRuntimeOptions *options);
int DobbyDisableRuntime(void);
int DobbyAutoHook(const DobbyAutoHookDescriptor *descriptor);
int DobbyAutoHookSymbol(const char *image_name,
                        const char *symbol_name,
                        void *replace_func,
                        void **origin_func);
int DobbyWaitAndHook(const char *image_name,
                     const char *symbol_name,
                     void *replace_func,
                     void **origin_func,
                     uint32_t timeout_ms);
int DobbyWaitAndHookOffset(const char *image_name,
                           uintptr_t offset,
                           void *replace_func,
                           void **origin_func,
                           uint32_t timeout_ms);
int DobbyAutoHookPendingCount(void);
```

## Recommended initialization

```c
static void *orig_target = 0;

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

## Descriptor-based symbol hook

Use `DobbyAutoHook` when you need explicit timeout, callback, backend, or delayed-first-attempt settings.

```c
static void on_hook_ready(int status,
                          const DobbyAutoHookDescriptor *descriptor,
                          void *resolved_target,
                          void *origin_func,
                          void *user_data) {
  (void)descriptor;
  (void)resolved_target;
  (void)origin_func;
  (void)user_data;
  // status == DOBBY_AUTOHOOK_STATUS_INSTALLED when the hook was installed.
}

void install_later(void *replacement, void **origin) {
  DobbyAutoHookDescriptor hook = {0};
  hook.image_name = "libtarget.so";
  hook.symbol_name = "target_func";
  hook.replace_func = replacement;
  hook.origin_func = origin;
  hook.retry_interval_ms = 100;
  hook.timeout_ms = 15000;
  hook.start_delay_ms = 300;
  hook.backend = DOBBY_HOOK_BACKEND_AUTO;
  hook.flags = DOBBY_AUTOHOOK_WAIT_MODULE |
               DOBBY_AUTOHOOK_WAIT_SYMBOL |
               DOBBY_AUTOHOOK_RETRY |
               DOBBY_AUTOHOOK_DELAY_FIRST;
  hook.callback = on_hook_ready;
  DobbyAutoHook(&hook);
}
```

## Offset hook

Offset hooks wait for the module base before computing `base + offset`.

```c
DobbyWaitAndHookOffset("libtarget.so", 0x1234, replacement, &origin, 10000);
```

## Failure model

The runtime retries only while prerequisites are missing: module not loaded, symbol not resolved, or mapping not executable. Once a concrete target is found and patching fails, the task is marked failed instead of retrying forever. This keeps the queue stable in long-running processes.

`DobbyDisableRuntime()` stops the worker and cancels pending tasks. Installed hooks are not automatically removed; use `DobbyDestroy`, `DobbyUnhook`, or the Android hook APIs to remove hooks explicitly.
