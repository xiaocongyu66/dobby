// ============================================================
// Android 便捷 API 实现 (老版本功能搬迁)
// ============================================================
#include "dobby_android_api.h"
#include "dobby_options.h"
#include "stealth.h"
#include <cstring>
#include "hooker.h"
#include "plt_hooker.h"
#include "symbols.h"
#include "waiter.h"

extern "C" {

int DobbyAndroidHookSymbol(const char *lib_name, const char *symbol_name,
                           void *replace, void **origin) {
    void *sym = dobby::Symbols::Find(lib_name, symbol_name);
    if (!sym) return DOBBY_ERR_NOT_FOUND;
    return dobby::Hooker::Hook(sym, replace, origin);
}

int DobbyAndroidHookOffset(const char *lib_name, uintptr_t offset,
                           void *replace, void **origin) {
    uintptr_t base = dobby::Symbols::Base(lib_name);
    if (!base) return DOBBY_ERR_NOT_FOUND;
    return dobby::Hooker::Hook((void *)(base + offset), replace, origin);
}

int DobbyAndroidHookPLT(const char *lib_name, const char *symbol_name,
                        void *replace, void **origin) {
    return dobby::PltHooker::Hook(lib_name, symbol_name, replace, origin);
}

int DobbyAndroidIsHooked(void *target) {
    return dobby::Hooker::IsHooked(target) ? 1 : 0;
}

int DobbyAndroidHookCount(void) { return dobby::Hooker::Count(); }
int DobbyAndroidWaitPending(void) { return dobby::Waiter::Pending(); }

}

// ===================== 高级自定义 hook (DobbyOptions) =====================

static bool opt_thumb(const DobbyOptions *o) {
    if (!o) return false;
    if (o->flags & DOBBY_HOOK_FORCE_THUMB) return true;
    if (o->flags & DOBBY_HOOK_FORCE_ARM) return false;
    return ((uintptr_t) nullptr) & 1;   // 默认按地址 bit0
}

int DobbyHookEx2(void *target, void *replace, void **origin, const DobbyOptions *opt) {
    if (!target || !replace) return DOBBY_ERR_INVALID;
    // 基础 hook
    int rc = dobby::Hooker::Hook(target, replace, origin);
    if (rc != DOBBY_OK) return rc;
    // flags 后处理:
    if (opt) {
        if (opt->flags & DOBBY_HOOK_DISGUISE_PAGE) {
            uintptr_t pg = (uintptr_t)target & ~0xFFFUL;
            dobby::Stealth::DisguisePage((void *)pg, 0x1000);
        }
        if (opt->flags & DOBBY_HOOK_VERIFY) {
            unsigned int head; memcpy(&head, target, sizeof(head));
            dobby::Stealth::SnapshotIntegrity(target, head);
        }
        // jump_encoding / head_len_override / 4BYTE / NEAR — 由 hooker 引擎逐版支持
        // (引擎当前实现: thumb 8B 头 + LDR 跳转; 其余参数保留)
    }
    return DOBBY_OK;
}

int DobbyPltHookEx(const char *lib, const char *symbol,
                   void *replace, void **origin, const DobbyOptions *opt) {
    (void)opt;
    return dobby::PltHooker::Hook(lib, symbol, replace, origin);
}

int DobbyWaitEx(const char *lib, const char *symbol, uintptr_t offset, int use_offset,
                void *replace, void **origin, const DobbyOptions *opt) {
    DobbyInit(false);   // 幂等
    uint32_t retry = opt ? opt->retry_ms : 0;
    uint32_t timeout = opt ? opt->timeout_ms : 0;
    dobby_wait_cb_t cb = opt ? opt->cb : nullptr;
    void *user = opt ? opt->user : nullptr;
    return dobby::Waiter::Submit(lib, symbol, offset, use_offset != 0,
                                 replace, origin, 0, timeout, cb, user);
}
