#include "dobby.h"
#include "dobby/common.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(__linux__) || defined(__ANDROID__)
#include <link.h>
#include <dlfcn.h>
#endif

#ifndef DOBBY_ANDROID_NAME_MAX
#define DOBBY_ANDROID_NAME_MAX 256
#endif

namespace {

struct HookEntry {
  std::string image;
  std::string symbol;
  uintptr_t offset;
  void *target;
  void *replace;
  void *origin;
  int enabled;
  int status;
};

std::mutex g_hook_lock;
std::vector<HookEntry> g_hooks;
char g_last_error[256] = "ok";

void set_last_error(const char *fmt, ...) {
  std::lock_guard<std::mutex> lock(g_hook_lock);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_last_error, sizeof(g_last_error), fmt ? fmt : "unknown error", ap);
  va_end(ap);
}

void copy_cstr(char *dst, size_t dst_size, const std::string &src) {
  if (!dst || dst_size == 0)
    return;
  size_t n = std::min(dst_size - 1, src.size());
  memcpy(dst, src.c_str(), n);
  dst[n] = '\0';
}

const char *basename_of(const char *path) {
  if (!path)
    return nullptr;
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

bool ends_with(const char *str, const char *suffix) {
  if (!str || !suffix)
    return false;
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);
  if (suffix_len > str_len)
    return false;
  return 0 == strcmp(str + str_len - suffix_len, suffix);
}

bool image_matches(const char *loaded_name, const char *image_name) {
  if (!image_name || image_name[0] == '\0')
    return true;
  if (!loaded_name || loaded_name[0] == '\0')
    return false;
  if (0 == strcmp(loaded_name, image_name))
    return true;

  const char *loaded_base = basename_of(loaded_name);
  const char *image_base = basename_of(image_name);
  return (loaded_base && image_base && 0 == strcmp(loaded_base, image_base)) || ends_with(loaded_name, image_name);
}

#if defined(__linux__) || defined(__ANDROID__)
struct FindModuleCtx {
  const char *image_name;
  uintptr_t base;
  const char *path;
};

int find_module_callback(struct dl_phdr_info *info, size_t size, void *data) {
  (void)size;
  FindModuleCtx *ctx = reinterpret_cast<FindModuleCtx *>(data);
  if (!ctx || !info)
    return 0;
  if (!image_matches(info->dlpi_name, ctx->image_name))
    return 0;
  ctx->base = static_cast<uintptr_t>(info->dlpi_addr);
  ctx->path = info->dlpi_name;
  return 1;
}
#endif

uintptr_t find_module_base_portable(const char *image_name) {
#if defined(__ANDROID__) && defined(DOBBY_ANDROID_USE_XDL)
  uintptr_t base = DobbyGetLibraryBase(image_name);
  if (base)
    return base;
#endif

#if defined(__linux__) || defined(__ANDROID__)
  FindModuleCtx ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.image_name = image_name;
  dl_iterate_phdr(find_module_callback, &ctx);
  return ctx.base;
#else
  (void)image_name;
  return 0;
#endif
}

void record_hook_locked(const char *image_name, const char *symbol_name, uintptr_t offset, void *target, void *replace,
                        void *origin, int enabled, int status) {
  auto it = std::find_if(g_hooks.begin(), g_hooks.end(), [target](const HookEntry &entry) { return entry.target == target; });
  HookEntry entry;
  entry.image = image_name ? image_name : "";
  entry.symbol = symbol_name ? symbol_name : "";
  entry.offset = offset;
  entry.target = target;
  entry.replace = replace;
  entry.origin = origin;
  entry.enabled = enabled;
  entry.status = status;

  if (it == g_hooks.end())
    g_hooks.push_back(entry);
  else
    *it = entry;
}

bool is_hooked_locked(void *target) {
  return std::any_of(g_hooks.begin(), g_hooks.end(), [target](const HookEntry &entry) {
    return entry.target == target && entry.enabled;
  });
}

int hook_target_common(const char *image_name, const char *symbol_name, uintptr_t offset, void *target, void *replace,
                       void **origin) {
  if (!target || !replace) {
    set_last_error("invalid hook arguments: target=%p replace=%p", target, replace);
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  {
    std::lock_guard<std::mutex> lock(g_hook_lock);
    if (is_hooked_locked(target)) {
      snprintf(g_last_error, sizeof(g_last_error), "target already hooked: %p", target);
      return DOBBY_ANDROID_ERR_ALREADY_HOOKED;
    }
  }

  void *origin_local = nullptr;
  void **origin_out = origin ? origin : &origin_local;
  int result = DobbyHook(target, replace, origin_out);
  if (result != 0) {
    set_last_error("DobbyHook failed: target=%p result=%d", target, result);
    return DOBBY_ANDROID_ERR_HOOK_FAILED;
  }

  {
    std::lock_guard<std::mutex> lock(g_hook_lock);
    record_hook_locked(image_name, symbol_name, offset, target, replace, origin_out ? *origin_out : nullptr, 1,
                       DOBBY_ANDROID_OK);
    snprintf(g_last_error, sizeof(g_last_error), "ok");
  }
  return DOBBY_ANDROID_OK;
}

} // namespace

extern "C" PUBLIC const char *DobbyAndroidStatusName(int code) {
  switch (code) {
  case DOBBY_ANDROID_OK:
    return "ok";
  case DOBBY_ANDROID_ERR_INVALID_ARGUMENT:
    return "invalid argument";
  case DOBBY_ANDROID_ERR_LIBRARY_NOT_FOUND:
    return "library not found";
  case DOBBY_ANDROID_ERR_SYMBOL_NOT_FOUND:
    return "symbol not found";
  case DOBBY_ANDROID_ERR_ALREADY_HOOKED:
    return "already hooked";
  case DOBBY_ANDROID_ERR_HOOK_FAILED:
    return "hook failed";
  case DOBBY_ANDROID_ERR_UNHOOK_FAILED:
    return "unhook failed";
  case DOBBY_ANDROID_ERR_BUFFER_TOO_SMALL:
    return "buffer too small";
  default:
    return "unknown";
  }
}

extern "C" PUBLIC const char *DobbyAndroidGetLastError() {
  std::lock_guard<std::mutex> lock(g_hook_lock);
  return g_last_error;
}

extern "C" PUBLIC uintptr_t DobbyAndroidGetModuleBase(const char *image_name) {
  if (!image_name || image_name[0] == '\0') {
    set_last_error("module name is empty");
    return 0;
  }
  uintptr_t base = find_module_base_portable(image_name);
  if (!base)
    set_last_error("library not found: %s", image_name);
  else
    set_last_error("ok");
  return base;
}

extern "C" PUBLIC void *DobbyAndroidFindSymbol(const char *image_name, const char *symbol_name) {
  if (!symbol_name || symbol_name[0] == '\0') {
    set_last_error("symbol name is empty");
    return nullptr;
  }

  void *addr = DobbySymbolResolverEx(image_name, symbol_name, DOBBY_SYMBOL_RESOLVER_DEFAULT, nullptr);
  if (!addr)
    set_last_error("symbol not found: image=%s symbol=%s", image_name ? image_name : "<all>", symbol_name);
  else
    set_last_error("ok");
  return addr;
}

extern "C" PUBLIC int DobbyAndroidHookSymbol(const char *image_name, const char *symbol_name, void *replace,
                                             void **origin) {
  if (!symbol_name || symbol_name[0] == '\0' || !replace) {
    set_last_error("invalid symbol hook args: image=%s symbol=%s replace=%p", image_name ? image_name : "<all>",
                   symbol_name ? symbol_name : "<null>", replace);
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  void *target = DobbyAndroidFindSymbol(image_name, symbol_name);
  if (!target)
    return DOBBY_ANDROID_ERR_SYMBOL_NOT_FOUND;
  return hook_target_common(image_name, symbol_name, 0, target, replace, origin);
}

extern "C" PUBLIC int DobbyAndroidHookOffset(const char *image_name, uintptr_t offset, void *replace, void **origin) {
  if (!image_name || image_name[0] == '\0' || !replace) {
    set_last_error("invalid offset hook args: image=%s offset=0x%zx replace=%p", image_name ? image_name : "<null>",
                   static_cast<size_t>(offset), replace);
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  uintptr_t base = DobbyAndroidGetModuleBase(image_name);
  if (!base)
    return DOBBY_ANDROID_ERR_LIBRARY_NOT_FOUND;

  void *target = reinterpret_cast<void *>(base + offset);
  return hook_target_common(image_name, nullptr, offset, target, replace, origin);
}

extern "C" PUBLIC int DobbyAndroidHookFunction(void *target, void *replace, void **origin) {
  return hook_target_common(nullptr, nullptr, 0, target, replace, origin);
}

extern "C" PUBLIC int DobbyAndroidUnhook(void *target) {
  if (!target) {
    set_last_error("unhook target is null");
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  int result = DobbyDestroy(target);
  if (result != 0) {
    set_last_error("DobbyDestroy failed: target=%p result=%d", target, result);
    return DOBBY_ANDROID_ERR_UNHOOK_FAILED;
  }

  {
    std::lock_guard<std::mutex> lock(g_hook_lock);
    for (auto &entry : g_hooks) {
      if (entry.target == target) {
        entry.enabled = 0;
        entry.status = DOBBY_ANDROID_OK;
      }
    }
    snprintf(g_last_error, sizeof(g_last_error), "ok");
  }
  return DOBBY_ANDROID_OK;
}

extern "C" PUBLIC int DobbyAndroidIsHooked(void *target) {
  if (!target)
    return 0;
  std::lock_guard<std::mutex> lock(g_hook_lock);
  return is_hooked_locked(target) ? 1 : 0;
}

extern "C" PUBLIC int DobbyAndroidListHooks(DobbyAndroidHookRecord *records, int max_count) {
  std::lock_guard<std::mutex> lock(g_hook_lock);
  int total = static_cast<int>(g_hooks.size());
  if (!records || max_count <= 0)
    return total;

  int count = std::min(total, max_count);
  for (int i = 0; i < count; i++) {
    memset(&records[i], 0, sizeof(records[i]));
    copy_cstr(records[i].image_name, sizeof(records[i].image_name), g_hooks[i].image);
    copy_cstr(records[i].symbol_name, sizeof(records[i].symbol_name), g_hooks[i].symbol);
    records[i].offset = g_hooks[i].offset;
    records[i].target_addr = g_hooks[i].target;
    records[i].replace_addr = g_hooks[i].replace;
    records[i].origin_addr = g_hooks[i].origin;
    records[i].enabled = g_hooks[i].enabled;
    records[i].status = g_hooks[i].status;
  }
  return total;
}

extern "C" PUBLIC int DobbyAndroidClearAllHooks() {
  std::vector<void *> targets;
  {
    std::lock_guard<std::mutex> lock(g_hook_lock);
    for (const auto &entry : g_hooks) {
      if (entry.enabled && entry.target)
        targets.push_back(entry.target);
    }
  }

  int first_error = DOBBY_ANDROID_OK;
  for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
    int result = DobbyAndroidUnhook(*it);
    if (result != DOBBY_ANDROID_OK && first_error == DOBBY_ANDROID_OK)
      first_error = result;
  }
  return first_error;
}
