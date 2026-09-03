// ============================================================
// dobby::InitArray::HookLinkerCall 实装
// 目标: linker 的 __dl_call_array (so 加载时执行 init_array 的入口)
// 符号获取: dlsym(RTLD_DEFAULT, "__dl_call_array") — linker 自身 dynsym
// hook: 函数头 inline (thumb/arm 按地址) → pre 回调 (so路径/init数组/数量)
// 降级: 符号找不到 → 返回 UNSUPPORTED (轮询 wait-hook 仍可用)
// ============================================================
#include "initarray.h"
#include "hooker.h"
#include "logger.h"
#include <dlfcn.h>
#include <string.h>

namespace dobby {

struct LinkerCallHook {
    void (*pre_cb)(const char *so_name, uintptr_t *init_array, size_t count, void *user);
    void *user;
    bool installed;
};
static LinkerCallHook g_lc = {nullptr, nullptr, false};

// linker call_array 的真实签名 (bionic linker_defs):
//   void call_array(LinkerTypeConstructorT** functions, size_t count, const char* caller_name?)
// 各版本不同 — 通用 pre: 假定 r0=functions, r1=count, r2=caller(或 soinfo)
// 我们不改行为, 只读观察 + 回调宿主.
using call_array_t = void (*)(uintptr_t *, size_t, const char *);
static call_array_t orig_call_array = nullptr;

// pre 回调转发: 记录参数后调原函数 (观察模式)
static void call_array_proxy(uintptr_t *functions, size_t count, const char *caller) {
    if (g_lc.pre_cb && functions && count) {
        // 尝试从回调里给宿主 so 名 (caller 可能为空 — 宿主可用 current so 推断)
        g_lc.pre_cb(caller ? caller : "", functions, count, g_lc.user);
    }
    orig_call_array(functions, count, caller);
}

int InitArray::HookLinkerCall(void *pre_cb, void *user) {
    void *sym = dlsym(RTLD_DEFAULT, "__dl_call_array");
    if (!sym) {
        DOBBY_LOG_W("linker __dl_call_array not found (Android version dependent)");
        return DOBBY_ERR_NOT_FOUND;
    }
    g_lc.pre_cb = (void (*)(const char *, uintptr_t *, size_t, void *))pre_cb;
    g_lc.user = user;
    orig_call_array = (call_array_t)sym;
    // 原函数是 linker 内部 — 用 PLT? 不, 它是函数本体 → inline hook:
    int rc = Hooker::Hook(sym, (void *)call_array_proxy, (void **)&orig_call_array);
    if (rc == DOBBY_OK) {
        g_lc.installed = true;
        DOBBY_LOG_I("linker __dl_call_array hooked @%p — init_array interception live", sym);
    }
    return rc;
}

} // namespace dobby
