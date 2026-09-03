// ============================================================
// dobby::LinkerMonitor — dlopen/dlclose 监听 (GlossLinkerHook 同款)
// 机制: hook android_dlopen_ext / __loader_dlopen (linker 导出) —
//       新 so 加载瞬间通知订阅者 (wait 队列加速/hook 事件).
// ============================================================
#include "linker_monitor.h"
#include "dobby.h"
#include <dlfcn.h>
#include "logger.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>

namespace dobby {

#define MAX_SUBS 16
struct Sub { void (*on_load)(const char *path, void *user); void *user; };
static Sub g_subs[MAX_SUBS];
static int g_sub_count = 0;

static void notify(const char *path) {
    for (int i = 0; i < g_sub_count; i++)
        if (g_subs[i].on_load) g_subs[i].on_load(path, g_subs[i].user);
}

// dlopen fake: 调原 → 成功则通知
static void *(*orig_dlopen_ext)(const char *, int, const void *) = nullptr;
static void *fake_dlopen_ext(const char *filename, int flags, const void *caller) {
    void *h = orig_dlopen_ext(filename, flags, caller);
    if (h && filename) {
        DOBBY_LOG_I("dlopen_ext: %s -> %p", filename, h);
        notify(filename);
    }
    return h;
}
static void *(*orig_loader_dlopen)(const char *, int, const void *) = nullptr;
static void *fake_loader_dlopen(const char *filename, int flags, const void *caller) {
    void *h = orig_loader_dlopen(filename, flags, caller);
    if (h && filename) {
        DOBBY_LOG_I("loader_dlopen: %s -> %p", filename, h);
        notify(filename);
    }
    return h;
}

bool LinkerMonitor::Start() {
    static bool started = false;
    if (started) return true;
    // 符号: libc 的 android_dlopen_ext (bionic 导出)
    void *libc = dlopen("libc.so", RTLD_NOW);
    if (!libc) return false;
    void *sym = dlsym(libc, "android_dlopen_ext");
    if (sym) {
        if (DobbyHook(sym, (void *)fake_dlopen_ext, (void **)&orig_dlopen_ext) == DOBBY_OK) {
            DOBBY_LOG_I("linker monitor: android_dlopen_ext hooked @%p", sym);
            started = true;
        }
    }
    dlclose(libc);
    return started;
}

bool LinkerMonitor::Subscribe(void (*on_load)(const char *path, void *user), void *user) {
    if (g_sub_count >= MAX_SUBS) return false;
    g_subs[g_sub_count++] = {on_load, user};
    Start();
    return true;
}

} // namespace dobby
