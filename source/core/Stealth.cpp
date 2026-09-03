// ============================================================
// dobby::Stealth — 反检测模块 (重构新增, 2026-09-04)
// 功能:
//   1. trampoline 匿名页 PR_SET_VMA 改名伪装 (防"未知匿名可执行段"扫描)
//   2. 跳转指令编码随机化 (防 CRC/模式特征匹配)
//   3. hook 完整性自检 (检测 TP 类引擎 unhook 恢复原始字节)
// 编译开关: DOBBY_STEALTH (CMake option, 默认 ON for Android)
// ============================================================
#include "dobby/common.h"
#include "Stealth.h"

#include <sys/prctl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#endif
#ifndef PR_SET_VMA_ANON_NAME
#define PR_SET_VMA_ANON_NAME 0
#endif

namespace dobby {

// 伪装名池: TP/ACE 扫描器对白名单段名放行 (libc/scudo/linker 系)
static const char *kCamouflageNames[] = {
    "libc_malloc", "scudo:secondary", "libc", "ld-android", "libart",
};

bool Stealth::DisguisePage(void *page_addr, size_t size) {
#if DOBBY_STEALTH
    if (!page_addr || size == 0)
        return false;
    // 选名: 按地址哈希轮换, 避免同库全部同名引起分布异常
    uintptr_t h = (uintptr_t)page_addr;
    const char *name = kCamouflageNames[h % (sizeof(kCamouflageNames) / sizeof(kCamouflageNames[0]))];
    int rc = prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, page_addr, size, name);
    return rc == 0;
#else
    return false;
#endif
}

// 跳转指令随机化: ARM32 提供 B / BLX(reg) / LDR PC 字面量 三种等价远跳编码;
// 每次分配 trampoline 时轮换, 相同 hook 布局产出不同字节序列 → CRC 快照无法稳定命中.
uint32_t Stealth::RandomizeJump(uint32_t preferred, void *from, void *to) {
#if DOBBY_STEALTH
    uintptr_t h = (uintptr_t)from ^ (uintptr_t)to;
    static uint32_t s_state = 0x9e3779b9;
    s_state ^= s_state << 13; s_state ^= s_state >> 17; s_state ^= s_state << 5;
    switch (s_state % 3) {
    case 0: return preferred;                       // 原生选择
    case 1: return 0xe51ff004;                      // ldr pc, [pc, #-4] (后跟字面量)
    case 2: return 0xe1a0f000 | ((h >> 4) & 0xf);   // mov pc, rx (寄存器跳转变体)
    }
#endif
    return preferred;
}

// hook 完整性自检: 读取 hook 点首字, 与安装时快照比较.
// TP 类引擎的 unhook = 恢复原始指令; 快照不一致 = 被 unhook → 回调告知宿主.
struct IntegrityEntry {
    void *addr;
    uint32_t installed_head;
};
static IntegrityEntry g_integrity[64];
static int g_integrity_count = 0;

void Stealth::SnapshotIntegrity(void *addr, uint32_t installed_head) {
    if (g_integrity_count < (int)(sizeof(g_integrity) / sizeof(g_integrity[0]))) {
        g_integrity[g_integrity_count++] = {addr, installed_head};
    }
}

bool Stealth::VerifyIntegrity(void *addr) {
    for (int i = 0; i < g_integrity_count; i++) {
        if (g_integrity[i].addr == addr) {
            uint32_t now;
            memcpy(&now, addr, sizeof(now));
            return now == g_integrity[i].installed_head;
        }
    }
    return true;
}

} // namespace dobby
