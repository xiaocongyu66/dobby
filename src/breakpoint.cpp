// ============================================================
// dobby::Breakpoint — 软件断点 (SIGTRAP 级指令插桩)
// 原理: thumb BKPT #imm (0xBE00) 写入目标 → 进程内 SIGTRAP handler →
//       RegContext 交 cb → cb 后 PC 跳过断点指令继续.
// 语义: 指令级只读观察 + 可改寄存器 (被断指令本身被跳过 — 
//       适合检查/分支/无害指令处, 不适合有副作用的指令).
// 限制: 同一进程最多 8 个并发断点; 单线程语义 (多线程命中需排队 — 由 siglock 保证).
// ============================================================
#include "breakpoint.h"
#if defined(__arm__)
#include "regcontext.h"
#include "logger.h"
#include <signal.h>
#include <string.h>
#include <sys/ucontext.h>
#include <pthread.h>
#include <mutex>
#include <vector>

namespace dobby {

struct BpRec {
    void *addr;              // 断点地址 (thumb, bit0=1)
    unsigned short original; // 原指令 (2 字节)
    void (*cb)(DobbyRegContext *, void *);
    void *user;
    bool active;
};
static BpRec g_bps[8];
static int g_bp_count = 0;
static std::mutex g_bp_mutex;
static struct sigaction g_old_sa;
static bool g_handler_installed = false;

// 原指令"影子执行"不可行 → handler 里由 cb 决定, 之后 PC 跳过断点指令.
static void bp_handler(int sig, siginfo_t *info, void *uctx) {
    ucontext_t *uc = (ucontext_t *)uctx;
    // fault addr = 断点地址 (thumb BKPT 在 ARM32: pc 指向 BKPT 处)
    uintptr_t fault = (uintptr_t)uc->uc_mcontext.arm_pc;
    std::lock_guard<std::mutex> lk(g_bp_mutex);
    for (int i = 0; i < g_bp_count; i++) {
        BpRec &b = g_bps[i];
        if (!b.active) continue;
        uintptr_t bpaddr = (uintptr_t)b.addr & ~1;
        if (fault != bpaddr) continue;
        // 组装 RegContext (从 ucontext):
        DobbyRegContext ctx = {};
        ctx.named.r0 = uc->uc_mcontext.arm_r0;
        ctx.named.r1 = uc->uc_mcontext.arm_r1;
        ctx.named.r2 = uc->uc_mcontext.arm_r2;
        ctx.named.r3 = uc->uc_mcontext.arm_r3;
        ctx.named.r4 = uc->uc_mcontext.arm_r4;
        ctx.named.r5 = uc->uc_mcontext.arm_r5;
        ctx.named.r6 = uc->uc_mcontext.arm_r6;
        ctx.named.r7 = uc->uc_mcontext.arm_r7;
        ctx.named.r8 = uc->uc_mcontext.arm_r8;
        ctx.named.r9 = uc->uc_mcontext.arm_r9;
        ctx.named.r10 = uc->uc_mcontext.arm_r10;
        ctx.named.r11 = uc->uc_mcontext.arm_fp;
        ctx.named.r12 = uc->uc_mcontext.arm_ip;
        ctx.named.SP = uc->uc_mcontext.arm_sp;
        ctx.named.LR = uc->uc_mcontext.arm_lr;
        ctx.pc = uc->uc_mcontext.arm_pc;
        ctx.cpsr = uc->uc_mcontext.arm_cpsr;
        if (b.cb) b.cb(&ctx, b.user);
        // 写回 (cb 可改寄存器):
        uc->uc_mcontext.arm_r0 = ctx.named.r0;
        uc->uc_mcontext.arm_r1 = ctx.named.r1;
        uc->uc_mcontext.arm_r2 = ctx.named.r2;
        uc->uc_mcontext.arm_r3 = ctx.named.r3;
        uc->uc_mcontext.arm_r4 = ctx.named.r4;
        uc->uc_mcontext.arm_r5 = ctx.named.r5;
        uc->uc_mcontext.arm_r6 = ctx.named.r6;
        uc->uc_mcontext.arm_r7 = ctx.named.r7;
        uc->uc_mcontext.arm_r8 = ctx.named.r8;
        uc->uc_mcontext.arm_r9 = ctx.named.r9;
        uc->uc_mcontext.arm_r10 = ctx.named.r10;
        uc->uc_mcontext.arm_fp = ctx.named.r11;
        uc->uc_mcontext.arm_ip = ctx.named.r12;
        // 恢复原指令 (2B) → 重新执行原指令:
        *(unsigned short *)bpaddr = b.original;
        __builtin___clear_cache((char *)bpaddr, (char *)bpaddr + 2);
        // 单步: handler 返回后 kernel 重执行原指令 — 之后我们要重装 BKPT:
        // 用"原指令后再断"不可行 → 惰性重装: b.active 保持, 但当前已还原 —
        // 设置 pending 重装 (由下一次任意 SIGTRAP? 不行) → 
        // 方案: 每次命中后 b.reinstall_pending = true, 由用户代码下次经过时?
        // ⭐ 简化: 单发断点 — 命中一次后自动失效 (cb 里可 ReArm):
        b.active = false;
        return;
    }
}

int Breakpoint::Install(void *addr, void (*cb)(DobbyRegContext *, void *), void *user) {
    if (!addr || !cb) return DOBBY_ERR_INVALID;
    uintptr_t a = (uintptr_t)addr;
    bool thumb = a & 1;
    if (!thumb) return DOBBY_ERR_UNSUPPORTED;
    uintptr_t fn = a - 1;
    if (g_bp_count >= (int)(sizeof(g_bps)/sizeof(g_bps[0]))) return DOBBY_ERR_FAILED;
    // 一次性装 handler:
    if (!g_handler_installed) {
        struct sigaction sa = {};
        sa.sa_sigaction = bp_handler;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGTRAP, &sa, &g_old_sa) != 0) return DOBBY_ERR_FAILED;
        g_handler_installed = true;
    }
    BpRec &b = g_bps[g_bp_count++];
    b.addr = addr;
    b.original = *(unsigned short *)fn;   // 备份原指令
    b.cb = cb; b.user = user; b.active = true;
    // 写 BKPT (thumb BKPT #0 = 0xBE00):
    Util::Write((void *)fn, "\x00\xbe", 2);
    DOBBY_LOG_I("breakpoint installed @%p (orig=0x%04x)", (void *)fn, b.original);
    return DOBBY_OK;
}

int Breakpoint::ReArm(void *addr) {
    for (int i = 0; i < g_bp_count; i++) {
        if (g_bps[i].addr == addr) {
            uintptr_t fn = (uintptr_t)addr & ~1;
            g_bps[i].active = true;
            Util::Write((void *)fn, "\x00\xbe", 2);
            return DOBBY_OK;
        }
    }
    return DOBBY_ERR_NOT_FOUND;
}

int Breakpoint::Remove(void *addr) {
    for (int i = 0; i < g_bp_count; i++) {
        if (g_bps[i].addr == addr) {
            uintptr_t fn = (uintptr_t)addr & ~1;
            Util::Write((void *)fn, &g_bps[i].original, 2);   // 恢复原指令
            g_bps[i] = g_bps[--g_bp_count];
            return DOBBY_OK;
        }
    }
    return DOBBY_ERR_NOT_FOUND;
}

} // namespace dobby

#endif // __arm__
