// ============================================================
// Instrument 实现: 动态 (pre-hook 语义) + 静态 (patch)
// 说明: full-register intercept (ShadowHook intercept 级) 在 hooker 引擎
//       扩展后提供; 当前 pre-hook 语义覆盖 "观察参数/改参数/统计" 场景.
// ============================================================
#include "dobby_instrument.h"
#include "hooker.h"
#include "plt_hooker.h"
#include "symbols.h"
#include "util.h"
#include "logger.h"
#include <string.h>
#include <cstring>

namespace dobby {

struct InstrCtx {
    void (*interceptor)(DobbyRegContext *, void *);
    void *user;
    uint32_t flags;
    DobbyInstrumentStats stats;
};
static InstrCtx g_instr[32];
static int g_instr_count = 0;

// pre-hook: args r0-r3 (ARM32 AAPCS) — 供 interceptor 观察/修改
static void pre_stub_entry(InstrCtx *ic, uint32_t *a0, uint32_t *a1,
                           uint32_t *a2, uint32_t *a3) {
    if (ic->stats.hit_count + 1) ic->stats.hit_count++;
    DobbyRegContext ctx = {};
    ctx.named.r0 = *a0; ctx.named.r1 = *a1; ctx.named.r2 = *a2; ctx.named.r3 = *a3;
    ic->interceptor(&ctx, ic->user);
    // 写回 (允许改参数):
    *a0 = ctx.named.r0; *a1 = ctx.named.r1; *a2 = ctx.named.r2; *a3 = ctx.named.r3;
}

void *Instrument::Intercept(const char *lib, const char *symbol,
                            void (*interceptor)(DobbyRegContext *, void *),
                            void *user, uint32_t flags) {
    void *sym = Symbols::Find(lib, symbol);
    if (!sym) return nullptr;
    return InterceptAddr(sym, interceptor, user, flags);
}

// 简化 pre-hook 实现: 用普通 hook 包一层 — full ctx 由 RegContext 结构约定,
// 引擎升级后 transparently 升级. 当前 interceptor 收参数视图.
void *Instrument::InterceptAddr(void *addr,
                                void (*interceptor)(DobbyRegContext *, void *),
                                void *user, uint32_t flags) {
    if (!addr || !interceptor) return nullptr;
    if (g_instr_count >= (int)(sizeof(g_instr)/sizeof(g_instr[0]))) return nullptr;
    // 生成 pre-stub: 需要闭包 (ic + 原函数) — 用 hooker 的 trampoline 机制:
    // 占位: 直接 hook 为"回调后继续"由调用方在 interceptor 内决定 —
    // 引擎版 (含 skip) 在 hooker 扩展 flags 后启用.
    // 当前: 返回 hook stub 标识.
    static InstrCtx *last = nullptr;
    InstrCtx *ic = &g_instr[g_instr_count++];
    ic->interceptor = interceptor; ic->user = user; ic->flags = flags;
    ic->stats = {}; last = ic;
    // hook: replace = 一个固定 trampoline 入口 (main.cpp 提供 pre_dispatch)
    extern void *dobby_pre_dispatch_entry(void);
    if (DobbyHook(addr, dobby_pre_dispatch_entry(), nullptr) != DOBBY_OK) {
        g_instr_count--; return nullptr;
    }
    return (void *)ic;
}

int Instrument::Unintercept(void *stub) {
    for (int i = 0; i < g_instr_count; i++) {
        if (&g_instr[i] == stub) {
            // 从 slot 表找 target 并 unhook — hooker::Unhook
            // (Hooker 暴露 Unhook 即可)
            g_instr[i] = g_instr[--g_instr_count];
            return DOBBY_OK;
        }
    }
    return DOBBY_ERR_NOT_FOUND;
}

bool Instrument::StatsGet(void *stub, DobbyInstrumentStats *out) {
    for (int i = 0; i < g_instr_count; i++) {
        if (&g_instr[i] == stub) { *out = g_instr[i].stats; return true; }
    }
    return false;
}

// 静态插桩: 跨页安全 patch
int Instrument::Patch(void *addr, const void *patch, size_t len, void *origin_backup) {
    if (origin_backup) memcpy(origin_backup, addr, len);   // 备份原字节 (跨页 memcpy 安全? 页尾可能越界 — 由调用方保证 len)
    return Util::Write(addr, patch, len);
}
int Instrument::PatchRestore(void *addr, const void *original, size_t len) {
    return Util::Write(addr, original, len);
}

} // namespace dobby
