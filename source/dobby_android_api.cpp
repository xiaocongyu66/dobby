#include "dobby.h"
#include "dobby/common.h"
#include "Runtime/HookRuntime.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(__linux__) || defined(__ANDROID__)
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

enum HookKind {
  kHookKindInline = 0,
  kHookKindPlt = 1,
  kHookKindVtable = 2,
};

struct PltPatch {
  void **slot = nullptr;
  void *origin = nullptr;
  int old_prot = PROT_READ;
};

struct HookEntry {
  std::string image;
  std::string symbol;
  uintptr_t offset = 0;
  void *target = nullptr;
  void *replace = nullptr;
  void *origin = nullptr;
  uintptr_t patch_addr = 0;
  size_t patch_size = 0;
  std::vector<PltPatch> plt_patches;
  int enabled = 0;
  int status = 0;
  int backend = DOBBY_HOOK_BACKEND_AUTO;
  int kind = kHookKindInline;
  uint64_t tx_id = 0;
};

std::mutex g_hook_lock;
std::mutex g_error_lock;
std::vector<HookEntry> g_hooks;
char g_last_error[256] = "ok";
uint64_t g_tx_current_id = 0;
int g_tx_depth = 0;

void set_last_error(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::lock_guard<std::mutex> lock(g_error_lock);
  vsnprintf(g_last_error, sizeof(g_last_error), fmt ? fmt : "unknown error", ap);
  va_end(ap);
}

void clear_last_error_locked() {
  std::lock_guard<std::mutex> lock(g_error_lock);
  snprintf(g_last_error, sizeof(g_last_error), "ok");
}

void copy_cstr(char *dst, size_t dst_size, const std::string &src) {
  if (!dst || dst_size == 0)
    return;
  size_t n = std::min(dst_size - 1, src.size());
  memcpy(dst, src.c_str(), n);
  dst[n] = '\0';
}

dobby::HookState to_runtime_state(int enabled, int status) {
  if (status != DOBBY_ANDROID_OK) {
    return dobby::HookState::Failed;
  }
  return enabled ? dobby::HookState::Enabled : dobby::HookState::Disabled;
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

struct PageProtection {
  bool read = false;
  bool write = false;
  bool exec = false;
};

#if defined(__linux__) || defined(__ANDROID__)
static PageProtection query_page_protection(void *addr) {
  PageProtection prot;
  FILE *fp = fopen("/proc/self/maps", "r");
  if (!fp)
    return prot;

  char line[512];
  uintptr_t query = reinterpret_cast<uintptr_t>(addr);
  while (fgets(line, sizeof(line), fp)) {
    uintptr_t start = 0, end = 0;
    char perms[5] = {0};
    if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) != 3)
      continue;
    if (query < start || query >= end)
      continue;
    prot.read = perms[0] == 'r';
    prot.write = perms[1] == 'w';
    prot.exec = perms[2] == 'x';
    break;
  }
  fclose(fp);
  return prot;
}

static int prot_to_mmap_flags(const PageProtection &prot) {
  int flags = 0;
  if (prot.read)
    flags |= PROT_READ;
  if (prot.write)
    flags |= PROT_WRITE;
  if (prot.exec)
    flags |= PROT_EXEC;
  return flags;
}
#endif

#if defined(__linux__) || defined(__ANDROID__)
struct FindModuleCtx {
  const char *image_name = nullptr;
  uintptr_t base = 0;
};

struct ModulePatcher {
  const char *image_name = nullptr;
  const char *symbol_name = nullptr;
  void *replace = nullptr;
  void **origin = nullptr;
  int backend = DOBBY_HOOK_BACKEND_PLT;
  int kind = kHookKindPlt;
  bool matched_any = false;
  bool patched_any = false;
  uintptr_t first_slot = 0;
  size_t patched_count = 0;
  std::vector<PltPatch> patches;
  std::string module_path;
  uint64_t tx_id = 0;
};

static inline uintptr_t page_align_down(uintptr_t addr, size_t page_size) {
  return addr & ~(page_size - 1);
}

static bool patch_pointer(void **slot, void *replace, void **origin, int *old_prot = nullptr) {
  if (!slot || !replace)
    return false;
  size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  uintptr_t page = page_align_down(reinterpret_cast<uintptr_t>(slot), page_size);
  PageProtection prev = query_page_protection(slot);
  int prev_prot = prot_to_mmap_flags(prev);
  if (old_prot)
    *old_prot = prev_prot;

  if (mprotect(reinterpret_cast<void *>(page), page_size, PROT_READ | PROT_WRITE) != 0) {
    ERROR_LOG("mprotect RW failed for slot=%p", slot);
    return false;
  }
  if (origin && !*origin)
    *origin = *slot;
  *slot = replace;
  __builtin___clear_cache(reinterpret_cast<char *>(slot), reinterpret_cast<char *>(slot) + sizeof(void *));
  if (mprotect(reinterpret_cast<void *>(page), page_size, prev_prot != 0 ? prev_prot : PROT_READ) != 0) {
    ERROR_LOG("mprotect restore failed for slot=%p", slot);
  }
  return true;
}

#if defined(__LP64__)
#define DOBBY_ELF_R_SYM ELF64_R_SYM
#else
#define DOBBY_ELF_R_SYM ELF32_R_SYM
#endif

template <typename RelT>
static int patch_relocation_array(uintptr_t base, const char *module_path, const char *symbol_name, const ElfW(Sym) *symtab,
                                  const char *strtab, const RelT *rels, size_t count, void *replace, void **origin,
                                  ModulePatcher *ctx) {
  int patched = 0;
  for (size_t i = 0; i < count; ++i) {
    uint32_t sym_idx = DOBBY_ELF_R_SYM(rels[i].r_info);
    const ElfW(Sym) *sym = &symtab[sym_idx];
    const char *name = strtab + sym->st_name;
    if (!name || strcmp(name, symbol_name) != 0)
      continue;

    auto *slot = reinterpret_cast<void **>(base + rels[i].r_offset);
    if (!slot)
      continue;
    int old_prot = PROT_READ;
    if (patch_pointer(slot, replace, origin, &old_prot)) {
      if (ctx && !ctx->patched_any)
        ctx->first_slot = reinterpret_cast<uintptr_t>(slot);
      if (ctx) {
        ctx->patched_any = true;
        ctx->patches.push_back({slot, origin ? *origin : nullptr, old_prot});
      }
      ++patched;
    }
  }
  (void)module_path;
  return patched;
}

static int patch_module_plt(const dl_phdr_info *info, ModulePatcher *ctx) {
  if (!info || !ctx || !ctx->symbol_name || !ctx->replace)
    return 0;
  if (!image_matches(info->dlpi_name, ctx->image_name))
    return 0;

  ctx->matched_any = true;
  ctx->module_path = info->dlpi_name ? info->dlpi_name : "";

  const ElfW(Phdr) *phdr = info->dlpi_phdr;
  const ElfW(Dyn) *dyn = nullptr;
  for (size_t i = 0; i < info->dlpi_phnum; ++i) {
    if (phdr[i].p_type == PT_DYNAMIC) {
      dyn = reinterpret_cast<const ElfW(Dyn) *>(info->dlpi_addr + phdr[i].p_vaddr);
      break;
    }
  }
  if (!dyn)
    return 0;

  const ElfW(Sym) *symtab = nullptr;
  const char *strtab = nullptr;
  const void *jmprel = nullptr;
  size_t jmprel_sz = 0;
  int jmprel_is_rela = 1;
  const void *rel_dyn = nullptr;
  size_t rel_dyn_sz = 0;
  int rel_dyn_is_rela = 1;

  for (const ElfW(Dyn) *it = dyn; it->d_tag != DT_NULL; ++it) {
    switch (it->d_tag) {
      case DT_SYMTAB:
        symtab = reinterpret_cast<const ElfW(Sym) *>(it->d_un.d_ptr);
        break;
      case DT_STRTAB:
        strtab = reinterpret_cast<const char *>(it->d_un.d_ptr);
        break;
      case DT_JMPREL:
        jmprel = reinterpret_cast<const void *>(it->d_un.d_ptr);
        break;
      case DT_PLTRELSZ:
        jmprel_sz = it->d_un.d_val;
        break;
      case DT_PLTREL:
        jmprel_is_rela = (it->d_un.d_val == DT_RELA) ? 1 : 0;
        break;
      case DT_RELA:
        rel_dyn = reinterpret_cast<const void *>(it->d_un.d_ptr);
        rel_dyn_is_rela = 1;
        break;
      case DT_RELASZ:
        rel_dyn_sz = it->d_un.d_val;
        break;
      case DT_REL:
        rel_dyn = reinterpret_cast<const void *>(it->d_un.d_ptr);
        rel_dyn_is_rela = 0;
        break;
      case DT_RELSZ:
        rel_dyn_sz = it->d_un.d_val;
        break;
      default:
        break;
    }
  }

  if (!symtab || !strtab)
    return 0;

  int patched = 0;
  uintptr_t base = static_cast<uintptr_t>(info->dlpi_addr);

  if (jmprel && jmprel_sz > 0) {
    if (jmprel_is_rela) {
      patched += patch_relocation_array(base, info->dlpi_name, ctx->symbol_name, symtab, strtab,
                                        reinterpret_cast<const ElfW(Rela) *>(jmprel), jmprel_sz / sizeof(ElfW(Rela)),
                                        ctx->replace, ctx->origin, ctx);
    } else {
      patched += patch_relocation_array(base, info->dlpi_name, ctx->symbol_name, symtab, strtab,
                                        reinterpret_cast<const ElfW(Rel) *>(jmprel), jmprel_sz / sizeof(ElfW(Rel)),
                                        ctx->replace, ctx->origin, ctx);
    }
  }

  if (rel_dyn && rel_dyn_sz > 0) {
    if (rel_dyn_is_rela) {
      patched += patch_relocation_array(base, info->dlpi_name, ctx->symbol_name, symtab, strtab,
                                        reinterpret_cast<const ElfW(Rela) *>(rel_dyn), rel_dyn_sz / sizeof(ElfW(Rela)),
                                        ctx->replace, ctx->origin, ctx);
    } else {
      patched += patch_relocation_array(base, info->dlpi_name, ctx->symbol_name, symtab, strtab,
                                        reinterpret_cast<const ElfW(Rel) *>(rel_dyn), rel_dyn_sz / sizeof(ElfW(Rel)),
                                        ctx->replace, ctx->origin, ctx);
    }
  }

  ctx->patched_count += static_cast<size_t>(patched);
  return 0;
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
  ctx.image_name = image_name;
#if defined(__ANDROID__) && defined(DOBBY_ANDROID_USE_XDL)
  xdl_iterate_phdr(
      [](struct dl_phdr_info *info, size_t size, void *data) -> int {
        (void)size;
        auto *ctx = reinterpret_cast<FindModuleCtx *>(data);
        if (!ctx || !info)
          return 0;
        if (!image_matches(info->dlpi_name, ctx->image_name))
          return 0;
        ctx->base = static_cast<uintptr_t>(info->dlpi_addr);
        return 1;
      },
      &ctx, XDL_FULL_PATHNAME);
#else
  dl_iterate_phdr(
      [](struct dl_phdr_info *info, size_t size, void *data) -> int {
        (void)size;
        auto *ctx = reinterpret_cast<FindModuleCtx *>(data);
        if (!ctx || !info)
          return 0;
        if (!image_matches(info->dlpi_name, ctx->image_name))
          return 0;
        ctx->base = static_cast<uintptr_t>(info->dlpi_addr);
        return 1;
      },
      &ctx);
#endif
  return ctx.base;
#else
  (void)image_name;
  return 0;
#endif
}

void record_hook_locked(const char *image_name, const char *symbol_name, uintptr_t offset, void *target, void *replace,
                       void *origin, uintptr_t patch_addr, size_t patch_size, int enabled, int status, int backend,
                       int kind, const std::vector<PltPatch> *plt_patches) {
  auto it = std::find_if(g_hooks.begin(), g_hooks.end(), [target](const HookEntry &entry) {
    return entry.target == target;
  });
  HookEntry entry;
  entry.image = image_name ? image_name : "";
  entry.symbol = symbol_name ? symbol_name : "";
  entry.offset = offset;
  entry.target = target;
  entry.replace = replace;
  entry.origin = origin;
  entry.patch_addr = patch_addr;
  entry.patch_size = patch_size;
  if (plt_patches) {
    entry.plt_patches = *plt_patches;
  }
  entry.enabled = enabled;
  entry.status = status;
  entry.backend = backend;
  entry.kind = kind;
  entry.tx_id = g_tx_depth > 0 ? g_tx_current_id : 0;

  if (it == g_hooks.end())
    g_hooks.push_back(entry);
  else
    *it = entry;

  dobby::HookRecord runtime_record;
  runtime_record.target = target;
  runtime_record.replace = replace;
  runtime_record.backup = origin;
  runtime_record.trampoline = nullptr;
  switch (backend) {
  case DOBBY_HOOK_BACKEND_INLINE:
    runtime_record.backend = dobby::HookBackend::Inline;
    break;
  case DOBBY_HOOK_BACKEND_PLT:
    runtime_record.backend = dobby::HookBackend::PLT;
    break;
  case DOBBY_HOOK_BACKEND_VTABLE:
    runtime_record.backend = dobby::HookBackend::VTable;
    break;
  case DOBBY_HOOK_BACKEND_AUTO:
  default:
    runtime_record.backend = dobby::HookBackend::Auto;
    break;
  }
  runtime_record.state = to_runtime_state(enabled, status);
  runtime_record.image = entry.image;
  runtime_record.symbol = entry.symbol;
  runtime_record.offset = offset;
  runtime_record.patch_addr = patch_addr;
  runtime_record.patch_size = patch_size;
  runtime_record.transaction_id = entry.tx_id;
  runtime_record.enabled = enabled != 0;
  dobby::HookRuntime::Shared().Upsert(runtime_record);
}

bool is_hooked_locked(void *target) {
  return std::any_of(g_hooks.begin(), g_hooks.end(), [target](const HookEntry &entry) {
    return entry.target == target && entry.enabled;
  });
}

bool restore_entry_locked(const HookEntry &entry) {
  if (!entry.enabled)
    return true;
  if (entry.kind == kHookKindInline) {
    return DobbyDestroy(entry.target) == 0;
  }

#if defined(__linux__) || defined(__ANDROID__)
  if (!entry.plt_patches.empty()) {
    bool ok = true;
    for (const auto &patch : entry.plt_patches) {
      if (!patch.slot)
        continue;
      size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
      uintptr_t page = page_align_down(reinterpret_cast<uintptr_t>(patch.slot), page_size);
      if (mprotect(reinterpret_cast<void *>(page), page_size, PROT_READ | PROT_WRITE) != 0) {
        ok = false;
        continue;
      }
      *patch.slot = patch.origin;
      __builtin___clear_cache(reinterpret_cast<char *>(patch.slot), reinterpret_cast<char *>(patch.slot) + sizeof(void *));
      if (mprotect(reinterpret_cast<void *>(page), page_size, patch.old_prot != 0 ? patch.old_prot : PROT_READ) != 0) {
        // best effort restore
      }
    }
    return ok;
  }

  auto *slot = reinterpret_cast<void **>(entry.target);
  if (!slot || !entry.origin)
    return false;
  size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  uintptr_t page = page_align_down(reinterpret_cast<uintptr_t>(slot), page_size);
  if (mprotect(reinterpret_cast<void *>(page), page_size, PROT_READ | PROT_WRITE) != 0)
    return false;
  *slot = entry.origin;
  __builtin___clear_cache(reinterpret_cast<char *>(slot), reinterpret_cast<char *>(slot) + sizeof(void *));
  if (mprotect(reinterpret_cast<void *>(page), page_size, PROT_READ) != 0) {
    // best effort restore
  }
  return true;
#else
  return false;
#endif
}

int hook_target_common(const char *image_name, const char *symbol_name, uintptr_t offset, void *target, void *replace,
                       void **origin, int backend, int kind) {
  if (!replace) {
    set_last_error("invalid hook arguments: target=%p replace=%p", target, replace);
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  if (backend != DOBBY_HOOK_BACKEND_PLT && !target) {
    set_last_error("invalid hook arguments: target=%p replace=%p", target, replace);
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  if (target && backend != DOBBY_HOOK_BACKEND_PLT) {
    std::lock_guard<std::mutex> lock(g_hook_lock);
    if (is_hooked_locked(target)) {
      snprintf(g_last_error, sizeof(g_last_error), "target already hooked: %p", target);
      return DOBBY_ANDROID_ERR_ALREADY_HOOKED;
    }
  }

  if (backend == DOBBY_HOOK_BACKEND_PLT || kind == kHookKindPlt) {
#if defined(__linux__) || defined(__ANDROID__)
    ModulePatcher ctx;
    ctx.image_name = image_name;
    ctx.symbol_name = symbol_name;
    ctx.replace = replace;
    ctx.origin = origin;
    ctx.backend = backend;
    ctx.kind = kind;
    ctx.tx_id = g_tx_current_id;
#if defined(__ANDROID__) && defined(DOBBY_ANDROID_USE_XDL)
    xdl_iterate_phdr(
        [](struct dl_phdr_info *info, size_t size, void *data) -> int {
          (void)size;
          auto *ctx = reinterpret_cast<ModulePatcher *>(data);
          if (!ctx || !info)
            return 0;
          patch_module_plt(info, ctx);
          if (ctx->patched_any)
            return 1;
          return 0;
        },
        &ctx, XDL_FULL_PATHNAME);
#else
    dl_iterate_phdr(
        [](struct dl_phdr_info *info, size_t size, void *data) -> int {
          (void)size;
          auto *ctx = reinterpret_cast<ModulePatcher *>(data);
          if (!ctx || !info)
            return 0;
          patch_module_plt(info, ctx);
          if (ctx->patched_any)
            return 1;
          return 0;
        },
        &ctx);
#endif

    if (!ctx.patched_any) {
      set_last_error("plt hook failed: image=%s symbol=%s", image_name ? image_name : "<all>",
                     symbol_name ? symbol_name : "<null>");
      return DOBBY_ANDROID_ERR_HOOK_FAILED;
    }

    std::lock_guard<std::mutex> lock(g_hook_lock);
    record_hook_locked(image_name, symbol_name, offset, reinterpret_cast<void *>(ctx.first_slot), replace,
                       origin ? *origin : nullptr, ctx.first_slot, ctx.patches.size() * sizeof(void *), 1,
                       DOBBY_ANDROID_OK, backend, kHookKindPlt, &ctx.patches);
    clear_last_error_locked();
    return DOBBY_ANDROID_OK;
#else
    (void)backend;
    (void)kind;
    set_last_error("plt backend unavailable");
    return DOBBY_ANDROID_ERR_BACKEND_UNSUPPORTED;
#endif
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
    record_hook_locked(image_name, symbol_name, offset, target, replace, origin_out ? *origin_out : nullptr,
                       reinterpret_cast<uintptr_t>(target), 0, 1, DOBBY_ANDROID_OK, backend, kind, nullptr);
    clear_last_error_locked();
  }
  return DOBBY_ANDROID_OK;
}

int unhook_by_target_locked(void *target) {
  auto it = std::find_if(g_hooks.begin(), g_hooks.end(), [target](const HookEntry &entry) { return entry.target == target; });
  if (it == g_hooks.end())
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;

  HookEntry entry = *it;
  if (!restore_entry_locked(entry))
    return DOBBY_ANDROID_ERR_UNHOOK_FAILED;

  void *target_key = entry.target;
  g_hooks.erase(it);
  dobby::HookRuntime::Shared().Remove(target_key);
  return DOBBY_ANDROID_OK;
}

int rollback_transaction_locked(uint64_t tx_id) {
  int first_error = DOBBY_ANDROID_OK;
  for (size_t i = g_hooks.size(); i > 0; --i) {
    size_t idx = i - 1;
    if (g_hooks[idx].tx_id != tx_id)
      continue;
    HookEntry entry = g_hooks[idx];
    if (!restore_entry_locked(entry) && first_error == DOBBY_ANDROID_OK)
      first_error = DOBBY_ANDROID_ERR_UNHOOK_FAILED;
    g_hooks.erase(g_hooks.begin() + static_cast<std::vector<HookEntry>::difference_type>(idx));
  }
  dobby::HookRuntime::Shared().RollbackTransaction(tx_id);
  return first_error;
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
  case DOBBY_ANDROID_ERR_BACKEND_UNSUPPORTED:
    return "backend unsupported";
  case DOBBY_ANDROID_ERR_TRANSACTION_ACTIVE:
    return "transaction active";
  default:
    return "unknown";
  }
}

extern "C" PUBLIC const char *DobbyAndroidGetLastError() {
  std::lock_guard<std::mutex> lock(g_error_lock);
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

extern "C" PUBLIC int DobbyAndroidHookBackend(void *target, void *replace, void **origin, DobbyHookBackend backend) {
  if (backend == DOBBY_HOOK_BACKEND_PLT || backend == DOBBY_HOOK_BACKEND_VTABLE) {
    set_last_error("backend requires symbol or object specific API");
    return DOBBY_ANDROID_ERR_BACKEND_UNSUPPORTED;
  }
  return hook_target_common(nullptr, nullptr, 0, target, replace, origin, backend, kHookKindInline);
}

extern "C" PUBLIC int DobbyAndroidHookSymbolBackend(const char *image_name, const char *symbol_name, void *replace,
                                                     void **origin, DobbyHookBackend backend) {
  if (!symbol_name || symbol_name[0] == '\0' || !replace) {
    set_last_error("invalid symbol hook args: image=%s symbol=%s replace=%p", image_name ? image_name : "<all>",
                   symbol_name ? symbol_name : "<null>", replace);
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  if (backend == DOBBY_HOOK_BACKEND_PLT) {
    return hook_target_common(image_name, symbol_name, 0, nullptr, replace, origin, backend, kHookKindPlt);
  }

  void *target = DobbyAndroidFindSymbol(image_name, symbol_name);
  if (target) {
    int result = hook_target_common(image_name, symbol_name, 0, target, replace, origin, DOBBY_HOOK_BACKEND_INLINE,
                                    kHookKindInline);
    if (result == DOBBY_ANDROID_OK || backend == DOBBY_HOOK_BACKEND_INLINE)
      return result;

    if (backend == DOBBY_HOOK_BACKEND_AUTO && image_name && image_name[0] != '\0') {
      return hook_target_common(image_name, symbol_name, 0, target, replace, origin, DOBBY_HOOK_BACKEND_PLT,
                                kHookKindPlt);
    }
    return result;
  }

  if (backend == DOBBY_HOOK_BACKEND_AUTO && image_name && image_name[0] != '\0') {
    return hook_target_common(image_name, symbol_name, 0, nullptr, replace, origin, DOBBY_HOOK_BACKEND_PLT,
                              kHookKindPlt);
  }

  return DOBBY_ANDROID_ERR_SYMBOL_NOT_FOUND;
}

extern "C" PUBLIC int DobbyAndroidHookSymbol(const char *image_name, const char *symbol_name, void *replace,
                                              void **origin) {
  return DobbyAndroidHookSymbolBackend(image_name, symbol_name, replace, origin, DOBBY_HOOK_BACKEND_AUTO);
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
  return hook_target_common(image_name, nullptr, offset, target, replace, origin, DOBBY_HOOK_BACKEND_INLINE,
                            kHookKindInline);
}

extern "C" PUBLIC int DobbyAndroidHookFunction(void *target, void *replace, void **origin) {
  return DobbyAndroidHookBackend(target, replace, origin, DOBBY_HOOK_BACKEND_INLINE);
}

extern "C" PUBLIC int DobbyAndroidHookPLT(const char *image_name, const char *symbol_name, void *replace,
                                           void **origin) {
  if (!symbol_name || symbol_name[0] == '\0' || !replace) {
    set_last_error("invalid plt hook args: image=%s symbol=%s replace=%p", image_name ? image_name : "<all>",
                   symbol_name ? symbol_name : "<null>", replace);
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }
  return hook_target_common(image_name, symbol_name, 0, nullptr, replace, origin, DOBBY_HOOK_BACKEND_PLT,
                            kHookKindPlt);
}

extern "C" PUBLIC int DobbyAndroidHookVtable(void *object, int vtable_index, void *replace, void **origin) {
  if (!object || vtable_index < 0 || !replace) {
    set_last_error("invalid vtable hook args: object=%p index=%d replace=%p", object, vtable_index, replace);
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  auto **vtable = *reinterpret_cast<void ***>(object);
  if (!vtable) {
    set_last_error("object has no vtable: %p", object);
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  void **slot = &vtable[vtable_index];
  if (DobbyAndroidIsHooked(slot))
    return DOBBY_ANDROID_ERR_ALREADY_HOOKED;

  void *origin_local = nullptr;
  void **origin_out = origin ? origin : &origin_local;
  int old_prot = PROT_READ;
  if (!patch_pointer(slot, replace, origin_out, &old_prot)) {
    set_last_error("vtable patch failed: object=%p index=%d", object, vtable_index);
    return DOBBY_ANDROID_ERR_HOOK_FAILED;
  }

  {
    std::lock_guard<std::mutex> lock(g_hook_lock);
    record_hook_locked(nullptr, nullptr, 0, slot, replace, origin_out ? *origin_out : nullptr,
                       reinterpret_cast<uintptr_t>(slot), sizeof(void *), 1, DOBBY_ANDROID_OK, DOBBY_HOOK_BACKEND_VTABLE,
                       kHookKindVtable, nullptr);
    clear_last_error_locked();
  }
  return DOBBY_ANDROID_OK;
}

extern "C" PUBLIC int DobbyAndroidUnhook(void *target) {
  if (!target) {
    set_last_error("unhook target is null");
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }

  std::lock_guard<std::mutex> lock(g_hook_lock);
  int result = unhook_by_target_locked(target);
  if (result != DOBBY_ANDROID_OK) {
    snprintf(g_last_error, sizeof(g_last_error), "DobbyAndroidUnhook failed: target=%p", target);
    return result;
  }
  clear_last_error_locked();
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
    records[i].patch_addr = g_hooks[i].patch_addr;
    records[i].patch_size = g_hooks[i].patch_size;
    records[i].enabled = g_hooks[i].enabled;
    records[i].status = g_hooks[i].status;
    records[i].backend = g_hooks[i].backend;
    records[i].kind = g_hooks[i].kind;
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

  {
    std::lock_guard<std::mutex> lock(g_hook_lock);
    g_tx_depth = 0;
    g_tx_current_id = 0;
    clear_last_error_locked();
  }
  dobby::HookRuntime::Shared().Clear();
  return first_error;
}

extern "C" PUBLIC int DobbyAndroidBeginTransaction() {
  std::lock_guard<std::mutex> lock(g_hook_lock);
  std::uint64_t tx_id = dobby::HookRuntime::Shared().BeginTransaction();
  ++g_tx_depth;
  if (g_tx_depth == 1) {
    g_tx_current_id = tx_id;
  }
  clear_last_error_locked();
  return DOBBY_ANDROID_OK;
}

extern "C" PUBLIC int DobbyAndroidCommitTransaction() {
  std::lock_guard<std::mutex> lock(g_hook_lock);
  if (g_tx_depth <= 0) {
    snprintf(g_last_error, sizeof(g_last_error), "no active transaction");
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }
  if (!dobby::HookRuntime::Shared().CommitTransaction()) {
    snprintf(g_last_error, sizeof(g_last_error), "runtime commit failed");
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }
  --g_tx_depth;
  if (g_tx_depth == 0)
    g_tx_current_id = 0;
  clear_last_error_locked();
  return DOBBY_ANDROID_OK;
}

extern "C" PUBLIC int DobbyAndroidRollbackTransaction() {
  std::lock_guard<std::mutex> lock(g_hook_lock);
  if (g_tx_depth <= 0) {
    snprintf(g_last_error, sizeof(g_last_error), "no active transaction");
    return DOBBY_ANDROID_ERR_INVALID_ARGUMENT;
  }
  uint64_t tx_id = g_tx_current_id;
  int result = rollback_transaction_locked(tx_id);
  g_tx_depth = 0;
  g_tx_current_id = 0;
  if (result != DOBBY_ANDROID_OK) {
    snprintf(g_last_error, sizeof(g_last_error), "transaction rollback failed");
    return result;
  }
  clear_last_error_locked();
  return DOBBY_ANDROID_OK;
}
