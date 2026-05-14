#include "module_scan_bypass.h"
#include "hide_module.h"
#include "dobby.h"

#include <link.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <mutex>

namespace dobby_stealth {

bool ModuleScanBypass::enabled_ = false;

static std::mutex g_filtered_lock;
static std::vector<std::string> g_filtered_modules;

// ============== 原始函数指针 ==============
static int (*orig_dl_iterate_phdr)(int (*)(struct dl_phdr_info *, size_t, void *), void *) = nullptr;

// ============== 判断模块是否应该被过滤 ==============
static bool should_filter_module(const char *name) {
  if (!name || name[0] == '\0') return false;

  std::lock_guard<std::mutex> lock(g_filtered_lock);
  for (const auto &filtered : g_filtered_modules) {
    if (strstr(name, filtered.c_str())) {
      return true;
    }
  }
  return false;
}

// ============== Hook: dl_iterate_phdr ==============
static int hooked_dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *),
                                   void *data) {
  // 包装原始回调，过滤掉隐藏的模块
  struct CallbackWrapper {
    int (*orig_callback)(struct dl_phdr_info *, size_t, void *);
    void *orig_data;
  };

  CallbackWrapper *wrapper = new CallbackWrapper;
  wrapper->orig_callback = callback;
  wrapper->orig_data = data;

  int result = orig_dl_iterate_phdr(
    [](struct dl_phdr_info *info, size_t size, void *data) -> int {
      auto *w = (CallbackWrapper *)data;

      // 检查模块名是否应该被过滤
      if (info->dlpi_name && should_filter_module(info->dlpi_name)) {
        return 0; // 跳过此模块
      }

      return w->orig_callback(info, size, w->orig_data);
    },
    wrapper
  );

  delete wrapper;
  return result;
}

// ============== Hook: dlopen (防止延迟加载的模块被发现) ==============
static void *(*orig_dlopen)(const char *, int) = nullptr;

static void *hooked_dlopen(const char *filename, int flags) {
  void *handle = orig_dlopen(filename, flags);

  // 如果刚加载的模块需要隐藏，立即从 solist 中移除
  if (handle && filename) {
    std::lock_guard<std::mutex> lock(g_filtered_lock);
    for (const auto &filtered : g_filtered_modules) {
      if (strstr(filename, filtered.c_str())) {
        LinkerSolistHider::HideModule(filename);
        break;
      }
    }
  }

  return handle;
}

// ============== Hook: android_dlopen_ext ==============
static void *(*orig_android_dlopen_ext)(const char *, int, const void *) = nullptr;

static void *hooked_android_dlopen_ext(const char *filename, int flags, const void *extinfo) {
  void *handle = orig_android_dlopen_ext(filename, flags, extinfo);

  if (handle && filename) {
    std::lock_guard<std::mutex> lock(g_filtered_lock);
    for (const auto &filtered : g_filtered_modules) {
      if (strstr(filename, filtered.c_str())) {
        LinkerSolistHider::HideModule(filename);
        break;
      }
    }
  }

  return handle;
}

// ============== 公共实现 ==============

bool ModuleScanBypass::Enable() {
  if (enabled_) return true;

  // 添加默认过滤模块
  AddFilteredModule("dobby");
  AddFilteredModule("substrate");
  AddFilteredModule("frida");

  // Hook dl_iterate_phdr
  void *dl_iterate_addr = DobbySymbolResolver("libc.so", "dl_iterate_phdr");
  if (dl_iterate_addr) {
    DobbyHook(dl_iterate_addr, (void *)hooked_dl_iterate_phdr,
              (void **)&orig_dl_iterate_phdr);
  }

  // Hook dlopen
  void *dlopen_addr = DobbySymbolResolver("libc.so", "dlopen");
  if (dlopen_addr) {
    DobbyHook(dlopen_addr, (void *)hooked_dlopen, (void **)&orig_dlopen);
  }

  // Hook android_dlopen_ext
  void *android_dlopen_ext_addr = DobbySymbolResolver("libdl.so", "android_dlopen_ext");
  if (!android_dlopen_ext_addr) {
    android_dlopen_ext_addr = DobbySymbolResolver("libc.so", "android_dlopen_ext");
  }
  if (android_dlopen_ext_addr) {
    DobbyHook(android_dlopen_ext_addr, (void *)hooked_android_dlopen_ext,
              (void **)&orig_android_dlopen_ext);
  }

  enabled_ = true;
  return true;
}

void ModuleScanBypass::Disable() {
  if (!enabled_) return;

  // 恢复所有 Hook
  void *addr = DobbySymbolResolver("libc.so", "dl_iterate_phdr");
  if (addr) DobbyDestroy(addr);

  addr = DobbySymbolResolver("libc.so", "dlopen");
  if (addr) DobbyDestroy(addr);

  addr = DobbySymbolResolver("libdl.so", "android_dlopen_ext");
  if (!addr) addr = DobbySymbolResolver("libc.so", "android_dlopen_ext");
  if (addr) DobbyDestroy(addr);

  {
    std::lock_guard<std::mutex> lock(g_filtered_lock);
    g_filtered_modules.clear();
  }

  enabled_ = false;
}

void ModuleScanBypass::AddFilteredModule(const char *module_name) {
  if (!module_name) return;
  std::lock_guard<std::mutex> lock(g_filtered_lock);
  g_filtered_modules.push_back(module_name);
}

} // namespace dobby_stealth
