#include "dobby.h"
#include "dobby/common.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <link.h>
#include <mutex>
#include <string>
#include <vector>

#if defined(__linux__) || defined(__ANDROID__)
#include <dlfcn.h>
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#ifndef DOBBY_ANDROID_NAME_MAX
#define DOBBY_ANDROID_NAME_MAX 256
#endif

namespace {

enum HookBackendKind {
  kHookBackendInline = 0,
  kHookBackendPlt = 1,
  kHookBackendVtable = 2,
};

struct HookEntry {
  std::string image;
  std::string symbol;
  uintptr_t offset;
  void *target;
  void *replace;
  void *origin;
  int enabled;
  int status;
  HookBackendKind backend;
};

struct ModuleInfo {
  std::string path;
  uintptr_t base;
  const ElfW(Phdr) *phdr;
  size_t phnum;
};

struct PltPatch {
  void **slot = nullptr;
  void *origin = nullptr;
};

std::mutex g_hook_lock;
std::vector<HookEntry> g_hooks;
char g_last_error[256] = "ok";

static void set_last_error(const char *fmt, ...) {
  std::lock_guard<std::mutex> lock(g_hook_lock);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_last_error, sizeof(g_last_error), fmt ? fmt : "unknown error", ap);
  va_end(ap);
}

static void copy_cstr(char *dst, size_t dst_size, const std::string &src) {
  if (!dst || dst_size == 0)
    return;
  size_t n = std::min(dst_size - 1, src.size());
  memcpy(dst, src.c_str(), n);
  dst[n] = '\0';
}

static const char *basename_of(const char *path) {
  if (!path)
    return nullptr;
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

static bool ends_with(const char *str, const char *suffix) {
  if (!str || !suffix)
    return false;
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);
  if (suffix_len > str_len)
    return false;
  return 0 == strcmp(str + str_len - suffix_len, suffix);
}

static bool image_matches(const char *loaded_name, const char *image_name) {
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
  std::vector<ModuleInfo> *modules;
};

static int find_module_callback(struct dl_phdr_info *info, size_t size, void *data) {
  (void)size;
  auto *ctx = reinterpret_cast<FindModuleCtx *>(data);
  if (!ctx || !info || !ctx->modules)
    return 0;
  if (!image_matches(info->dlpi_name, ctx->image_name))
    return 0;

  ModuleInfo module;
  module.path = info->dlpi_name ? info->dlpi_name : "";
  module.base = static_cast<uintptr_t>(info->dlpi_addr);
  module.phdr = info->dlpi_phdr;
  module.phnum = info->dlpi_phnum;
  if (module.base && module.phdr && module.phnum)
    ctx->modules->push_back(module);
  return 0;
}

static std::vector<ModuleInfo> find_modules(const char *image_name) {
  std::vector<ModuleInfo> modules;
  FindModuleCtx ctx;
  ctx.image_name = image_name;
  ctx.modules = &modules;
  dl_iterate_phdr(find_module_callback, &ctx);
  return modules;
}

#endif

static bool page_write_ptr(void **slot, void *value, void **old_value) {
#if defined(__linux__) || defined(__ANDROID__)
  if (!slot)
    return false;
  uintptr_t page = reinterpret_cast<uintptr_t>(slot) & ~(static_cast<uintptr_t>(getpagesize()) - 1);
  if (0 != mprotect(reinterpret_cast<void *>(page), static_cast<size_t>(getpagesize()), PROT_READ | PROT_WRITE))
    return false;
  if (old_value)
    *old_value = *slot;
  *slot = value;
  __builtin___clear_cache(reinterpret_cast<char *>(slot), reinterpret_cast<char *>(slot) + sizeof(void *));
  mprotect(reinterpret_cast<void *>(page), static_cast<size_t>(getpagesize()), PROT_READ);
  return true;
#else
  (void)slot;
  (void)value;
  (void)old_value;
  return false;
#endif
}

static bool page_restore_ptr(void **slot, void *value) {
#if defined(__linux__) || defined(__ANDROID__)
  if (!slot)
    return false;
  uintptr_t page = reinterpret_cast<uintptr_t>(slot) & ~(static_cast<uintptr_t>(getpagesize()) - 1);
  if (0 != mprotect(reinterpret_cast<void *>(page), static_cast<size_t>(getpagesize()), PROT_READ | PROT_WRITE))
    return false;
  *slot = value;
  __builtin___clear_cache(reinterpret_cast<char *>(slot), reinterpret_cast<char *>(slot) + sizeof(void *));
  mprotect(reinterpret_cast<void *>(page), static_cast<size_t>(getpagesize()), PROT_READ);
  return true;
#else
  (void)slot;
  (void)value;
  return false;
#endif
}

#if defined(__linux__) || defined(__ANDROID__)

#if defined(__LP64__) || defined(__aarch64__) || defined(__x86_64__)
#define DOBBY_RELOC_SYM(info) ELF64_R_SYM(info)
#else
#define DOBBY_RELOC_SYM(info) ELF32_R_SYM(info)
#endif

static bool patch_plt_in_module(const ModuleInfo &module, const char *symbol_name, void *replace_func,
                                PltPatch *patch) {
  if (!patch || !module.base || !module.phdr || !symbol_name || symbol_name[0] == '\0' || !replace_func)
    return false;

  auto *ehdr = reinterpret_cast<const ElfW(Ehdr) *>(module.base);
  if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0)
    return false;

  uintptr_t dyn_addr = 0;
  size_t dyn_size = 0;
  for (size_t i = 0; i < module.phnum; i++) {
    if (module.phdr[i].p_type == PT_DYNAMIC) {
      dyn_addr = module.base + module.phdr[i].p_vaddr;
      dyn_size = module.phdr[i].p_memsz;
      break;
    }
  }
  if (!dyn_addr || !dyn_size)
    return false;

  uintptr_t jmprel_addr = 0;
  size_t jmprel_size = 0;
  uintptr_t symtab_addr = 0;
  uintptr_t strtab_addr = 0;
  int pltrel_tag = 0;

  const auto *dyn = reinterpret_cast<const ElfW(Dyn) *>(dyn_addr);
  for (size_t i = 0; i < dyn_size / sizeof(ElfW(Dyn)); i++) {
    switch (dyn[i].d_tag) {
      case DT_NULL:
        i = dyn_size / sizeof(ElfW(Dyn));
        break;
      case DT_JMPREL:
        jmprel_addr = dyn[i].d_un.d_ptr;
        break;
      case DT_PLTRELSZ:
        jmprel_size = dyn[i].d_un.d_val;
        break;
      case DT_SYMTAB:
        symtab_addr = dyn[i].d_un.d_ptr;
        break;
      case DT_STRTAB:
        strtab_addr = dyn[i].d_un.d_ptr;
        break;
      case DT_PLTREL:
        pltrel_tag = static_cast<int>(dyn[i].d_un.d_val);
        break;
    }
  }

  if (!jmprel_addr || !jmprel_size || !symtab_addr || !strtab_addr)
    return false;

  const char *strtab = reinterpret_cast<const char *>(strtab_addr);
  auto *symtab = reinterpret_cast<const ElfW(Sym) *>(symtab_addr);

#if defined(__LP64__) || defined(__aarch64__) || defined(__x86_64__)
  if (pltrel_tag != DT_RELA && pltrel_tag != 0)
    return false;
  auto *rela = reinterpret_cast<const ElfW(Rela) *>(jmprel_addr);
  size_t count = jmprel_size / sizeof(ElfW(Rela));
  for (size_t i = 0; i < count; i++) {
    size_t sym_idx = DOBBY_RELOC_SYM(rela[i].r_info);
    const char *name = strtab + symtab[sym_idx].st_name;
    if (!name || 0 != strcmp(name, symbol_name))
      continue;
    auto *got_entry = reinterpret_cast<void **>(module.base + rela[i].r_offset);
    if (!page_write_ptr(got_entry, replace_func, &patch->origin))
      return false;
    patch->slot = got_entry;
    return true;
  }
#else
  if (pltrel_tag != DT_REL && pltrel_tag != 0)
    return false;
  auto *rel = reinterpret_cast<const ElfW(Rel) *>(jmprel_addr);
  size_t count = jmprel_size / sizeof(ElfW(Rel));
  for (size_t i = 0; i < count; i++) {
    size_t sym_idx = DOBBY_RELOC_SYM(rel[i].r_info);
    const char *name = strtab + symtab[sym_idx].st_name;
    if (!name || 0 != strcmp(name, symbol_name))
      continue;
    auto *got_entry = reinterpret_cast<void **>(module.base + rel[i].r_offset);
    if (!page_write_ptr(got_entry, replace_func, &patch->origin))
      return false;
    patch->slot = got_entry;
    return true;
  }
#endif

  return false;
}

#endif

static void record_hook_locked(const char *image_name, const char *symbol_name, uintptr_t offset, void *target,
                               void *replace, void *origin, int enabled, int status, HookBackendKind backend) {
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
  entry.backend = backend;

  if (it == g_hooks.end())
    g_hooks.push_back(entry);
  else
    *it = entry;
}

static bool is_hooked_locked(void *target) {
  return std::any_of(g_hooks.begin(), g_hooks.end(), [target](const HookEntry &entry) {
    return entry.target == target && entry.enabled;
  });
}

static int hook_inline_common(const char *image_name, const char *symbol_name, uintptr_t offset, void *target,
                              void *replace, void **origin) {
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
                       DOBBY_ANDROID_OK, kHookBackendInline);
    snprintf(g_last_error, sizeof(g_last_error), "ok");
  }
  return DOBBY_ANDROID_OK;
}

static int hook_plt_common(const char *image_name, const char *symbol_name, void *replace, void **origin) {
  if (!symbol_name || symbol_name[0] == '\0' || !replace) {
    set_last_error("invalid plt hook args: image=%s symbol=%s replace=%p", image_name ? image_name : "<all>",
                   symbol_name ? symbol_name : "<null>", replace);
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  std::vector<ModuleInfo> modules = find_modules(image_name);
  if (modules.empty()) {
    set_last_error("module not found: %s", image_name ? image_name : "<all>");
    return DOBBY_ANDROID_ERR_LIBRARY_NOT_FOUND;
  }

  bool patched_any = false;
  void *origin_local = nullptr;
  void **origin_out = origin ? origin : &origin_local;

  for (const auto &module : modules) {
    PltPatch patch;
    if (!patch_plt_in_module(module, symbol_name, replace, &patch))
      continue;

    patched_any = true;
    if (origin_out && !*origin_out)
      *origin_out = patch.origin;

    std::lock_guard<std::mutex> lock(g_hook_lock);
    record_hook_locked(module.path.c_str(), symbol_name, 0, patch.slot, replace, patch.origin, 1, DOBBY_ANDROID_OK,
                       kHookBackendPlt);

    if (image_name && image_name[0] != '\0')
      break;
  }

  if (patched_any) {
    set_last_error("ok");
    return DOBBY_ANDROID_OK;
  }

  set_last_error("plt hook failed: image=%s symbol=%s", image_name ? image_name : "<all>", symbol_name);
  return DOBBY_ANDROID_ERR_HOOK_FAILED;
}

static int unhook_record_locked(const HookEntry &entry) {
  if (!entry.target)
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;

  switch (entry.backend) {
    case kHookBackendInline:
      if (0 != DobbyDestroy(entry.target))
        return DOBBY_ANDROID_ERR_UNHOOK_FAILED;
      return DOBBY_ANDROID_OK;
    case kHookBackendPlt:
    case kHookBackendVtable:
      if (!page_restore_ptr(reinterpret_cast<void **>(entry.target), entry.origin))
        return DOBBY_ANDROID_ERR_UNHOOK_FAILED;
      return DOBBY_ANDROID_OK;
    default:
      return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }
}

static HookEntry *find_hook_entry_locked(void *target) {
  auto it = std::find_if(g_hooks.begin(), g_hooks.end(), [target](const HookEntry &entry) { return entry.target == target; });
  if (it == g_hooks.end())
    return nullptr;
  return &(*it);
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

#if defined(__ANDROID__) && defined(DOBBY_ANDROID_USE_XDL)
  uintptr_t base = DobbyGetLibraryBase(image_name);
  if (base) {
    set_last_error("ok");
    return base;
  }
#endif

#if defined(__linux__) || defined(__ANDROID__)
  std::vector<ModuleInfo> modules = find_modules(image_name);
  if (!modules.empty()) {
    set_last_error("ok");
    return modules.front().base;
  }
#endif

  set_last_error("library not found: %s", image_name);
  return 0;
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

extern "C" PUBLIC int DobbyAndroidHookFunction(void *target, void *replace, void **origin) {
  return hook_inline_common(nullptr, nullptr, 0, target, replace, origin);
}

extern "C" PUBLIC int DobbyAndroidHookPLT(const char *image_name, const char *symbol, void *replace, void **origin);

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
  return hook_inline_common(image_name, symbol_name, 0, target, replace, origin);
}

extern "C" PUBLIC int DobbyAndroidHookBySymbolSmart(const char *image_name, const char *symbol_name, void *replace,
                                                     void **origin) {
  int result = DobbyAndroidHookSymbol(image_name, symbol_name, replace, origin);
  if (result == DOBBY_ANDROID_OK)
    return result;
  return DobbyAndroidHookPLT(image_name, symbol_name, replace, origin);
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
  return hook_inline_common(image_name, nullptr, offset, target, replace, origin);
}

extern "C" PUBLIC int DobbyAndroidHookPLT(const char *image_name, const char *symbol, void *replace, void **origin) {
  return hook_plt_common(image_name, symbol, replace, origin);
}

extern "C" PUBLIC int DobbyImportTableReplace(char *image_name, char *symbol_name, void *fake_func, void **orig_func) {
  return DobbyAndroidHookPLT(image_name, symbol_name, fake_func, orig_func);
}

extern "C" PUBLIC int DobbyAndroidHookVtable(void *object, int vtable_index, void *replace, void **origin) {
  if (!object || vtable_index < 0 || !replace) {
    set_last_error("invalid vtable hook args: object=%p index=%d replace=%p", object, vtable_index, replace);
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  void ***vtable_ptr = reinterpret_cast<void ***>(object);
  if (!vtable_ptr || !*vtable_ptr) {
    set_last_error("vtable pointer is null: object=%p", object);
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  void **vtable = *vtable_ptr;
  void **slot = &vtable[vtable_index];
  void *old_value = nullptr;
  if (!page_write_ptr(slot, replace, &old_value)) {
    set_last_error("failed to patch vtable slot: object=%p index=%d", object, vtable_index);
    return DOBBY_ANDROID_ERR_HOOK_FAILED;
  }

  if (origin)
    *origin = old_value;

  {
    std::lock_guard<std::mutex> lock(g_hook_lock);
    record_hook_locked(nullptr, nullptr, 0, slot, replace, old_value, 1, DOBBY_ANDROID_OK, kHookBackendVtable);
    set_last_error("ok");
  }
  return DOBBY_ANDROID_OK;
}

extern "C" PUBLIC int DobbyAndroidHookShortFunction(void *target, void *replace, void **origin) {
  if (!target || !replace) {
    set_last_error("invalid short-function hook args: target=%p replace=%p", target, replace);
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  int result = DobbyAndroidHookFunction(target, replace, origin);
  if (result == DOBBY_ANDROID_OK)
    return result;

  dobby_set_near_trampoline(true);
  result = DobbyAndroidHookFunction(target, replace, origin);
  if (result == DOBBY_ANDROID_OK)
    return result;

  set_last_error("short function hook failed: target=%p", target);
  return result;
}

extern "C" PUBLIC void DobbyAndroidEnableNearBranchTrampoline(void) {
  dobby_set_near_trampoline(true);
}

extern "C" PUBLIC int DobbyAndroidUnhook(void *target) {
  if (!target) {
    set_last_error("unhook target is null");
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  int result = DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  {
    std::lock_guard<std::mutex> lock(g_hook_lock);
    HookEntry *entry = find_hook_entry_locked(target);
    if (entry) {
      result = unhook_record_locked(*entry);
      if (result == DOBBY_ANDROID_OK) {
        entry->enabled = 0;
        entry->status = DOBBY_ANDROID_OK;
      }
    }
  }

  if (result != DOBBY_ANDROID_OK) {
    set_last_error("unhook failed: target=%p", target);
    return DOBBY_ANDROID_ERR_UNHOOK_FAILED;
  }

  set_last_error("ok");
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
    records[i].backend = static_cast<int>(g_hooks[i].backend);
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
