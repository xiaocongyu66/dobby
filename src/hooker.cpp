// ============================================================
// dobby::Hooker — 自包含 Inline Hook (ARM32 thumb/arm)
// 原理: 函数头指令复制到 trampoline + PC-rel 修正; 函数头改写绝对跳转.
// thumb (bit0=1): 8B 覆盖 (4 条 thumb) → trampoline 补 LDR PC 跳回
// arm:            8B 覆盖 (2 条)     → trampoline 补 LDR PC 跳回
// 头改写: thumb: LDR.W PC,[PC] + 字面量; arm: LDR PC,[PC,#-4] + 字面量
// ============================================================
#include "hooker.h"
#include "logger.h"
#include "events.h"
#include "util.h"
#include "stealth.h"
#include "stealth.h"
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>

namespace dobby {

struct HookSlot {
    void *target;       // 原函数 (thumb: bit0)
    void *replace;
    void *trampoline;   // 原指令副本 + 跳回
    size_t head_len;
};

static HookSlot g_slots[128];
static int g_slot_count = 0;
static int FindSlot(void *target) {
    for (int i = 0; i < g_slot_count; i++)
        if (g_slots[i].target == target) return i;
    return -1;
}

// PC-rel 指令 (B/BL/ADR) 的重定位修正 (thumb 16/32 混合 — 常用子集):
// 覆盖不了的模式返回 false → 该函数不适合本简化引擎 (调用方报错).
static bool RelocateThumb(unsigned char *dst, const unsigned char *src, size_t len,
                          uintptr_t src_pc, uintptr_t dst_pc) {
    memcpy(dst, src, len);
    // 逐半字扫描, 修正 BL (0xF000F800 / 0xF000D000 系)
    for (size_t i = 0; i + 4 <= len; i += 2) {
        unsigned short hw = *(unsigned short *)(src + i);
        if ((hw & 0xF800) == 0xF000) {
            unsigned short hw2 = *(unsigned short *)(src + i + 2);
            unsigned kind = hw2 & 0x2800;   // D000=BL, 2800=BLX
            if (kind == 0x0000 || kind == 0x2800) {
                bool is_blx = (kind == 0x2800);
                unsigned S = (hw >> 10) & 1, imm10 = hw & 0x3FF;
                unsigned J1 = (hw2 >> 13) & 1, J2 = (hw2 >> 11) & 1, imm11 = hw2 & 0x7FF;
                unsigned I1 = 1 - (J1 ^ S), I2 = 1 - (J2 ^ S);
                int32_t off = (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | (imm11 << 1);
                if (off & 0x800000) off -= 1 << 24;
                uintptr_t old_target = src_pc + i + 4 + off;
                if (is_blx) old_target &= ~3;
                int32_t new_off = (int32_t)(old_target - (dst_pc + i + 4));
                if (is_blx) new_off &= ~3;
                if (new_off > 0x7FFFFF || new_off < -(0x800000))
                    return false;   // 距离超限 (极罕见 — trampoline 在近页)
                unsigned nS = (new_off >> 24) & 1;
                unsigned nI1 = (new_off >> 23) & 1, nI2 = (new_off >> 22) & 1;
                unsigned nimm10 = (new_off >> 12) & 0x3FF, nimm11 = (new_off >> 1) & 0x7FF;
                unsigned short nh = 0xF000 | (nS << 10) | nimm10;
                unsigned short nh2 = (is_blx ? 0xC000 : 0xD000) | (nI1 << 13) | (nI2 << 11) | nimm11;
                *(unsigned short *)(dst + i) = nh;
                *(unsigned short *)(dst + i + 2) = nh2;
                i += 2;   // 32 位指令
            }
        }
    }
    return true;
}

int Hooker::Hook(void *target, void *replace, void **origin) {
    if (!target || !replace) return DOBBY_ERR_INVALID;
    uintptr_t a = (uintptr_t)target;
    bool thumb = a & 1;
    uintptr_t fn = thumb ? (a - 1) : a;
    if (FindSlot(target) >= 0) return DOBBY_ERR_ALREADY;
    if (g_slot_count >= (int)(sizeof(g_slots)/sizeof(g_slots[0]))) return DOBBY_ERR_FAILED;

    // ARM 模式分支: 头 8B (2 条指令) 复制 + PC-rel 修正 + LDR PC 跳回
    if (!thumb) {
        size_t HEAD = 8;
        unsigned char *tramp = (unsigned char *)mmap(nullptr, 0x1000, PROT_READ|PROT_WRITE|PROT_EXEC,
                                                     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (tramp == MAP_FAILED) return DOBBY_ERR_FAILED;
        Stealth::DisguisePage(tramp, 0x1000);
        // 复制 2 条 ARM 指令 (B/BL 的 PC-rel 修正暂缺 — 函数头 B 罕见):
        memcpy(tramp, (void *)fn, HEAD);
        // 尾部 LDR PC 跳回: ldr pc, [pc, #-4] + 字面量
        *(unsigned int *)(tramp + HEAD) = 0xE51FF004;
        *(unsigned int *)(tramp + HEAD + 4) = (unsigned int)(fn + HEAD);
        // 函数头改写: ldr pc, [pc, #-4] + 字面量 → replace (arm 地址无 bit0)
        if (Util::Protect((void *)fn, HEAD, PROT_READ|PROT_WRITE|PROT_EXEC) != 0)
            { munmap(tramp, 0x1000); return DOBBY_ERR_FAILED; }
        *(unsigned int *)fn = 0xE51FF004;
        *(unsigned int *)(fn + 4) = (unsigned int)(uintptr_t)replace;
        Util::Flush((void *)fn, HEAD);
        Stealth::SnapshotIntegrity(target, *(unsigned int *)tramp);
        HookSlot &s = g_slots[g_slot_count++];
        s.target = target; s.replace = replace; s.trampoline = tramp; s.head_len = HEAD;
        if (origin) *origin = (void *)tramp;
        DOBBY_LOG_I("hooked %p -> %p (arm mode)", target, replace);
        return DOBBY_OK;
    }

    const size_t HEAD = 8;   // thumb 4 条
    // trampoline: 原指令 + 跳回 (ldr.w pc + 字面量 = 8B)
    size_t tramp_len = HEAD + 8;
    unsigned char *tramp = (unsigned char *)mmap(nullptr, 0x1000, PROT_READ|PROT_WRITE|PROT_EXEC,
                                                 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (tramp == MAP_FAILED) return DOBBY_ERR_FAILED;
    // 伪装 (stealth 模块)
    Stealth::DisguisePage(tramp, 0x1000);

    uintptr_t tramp_pc = (uintptr_t)tramp + 4;   // thumb: pc = addr+4
    if (!RelocateThumb(tramp, (unsigned char *)fn, HEAD, fn + 4, tramp_pc))
        { munmap(tramp, 0x1000); return DOBBY_ERR_UNSUPPORTED; }
    // trampoline 尾: 跳回 fn+HEAD (thumb LDR.W PC)
    unsigned char *back = tramp + HEAD;
    *(unsigned short *)back = 0xF8DF; *(unsigned short *)(back+2) = 0x0000; // ldr.w pc,[pc]
    *(unsigned int *)(back + 4) = (unsigned int)(fn + HEAD + 1);  // thumb 位

    // 函数头改写: ldr.w pc,[pc,#0] + 字面量 → replace
    if (Util::Protect((void *)fn, HEAD, PROT_READ|PROT_WRITE|PROT_EXEC) != 0)
        { munmap(tramp, 0x1000); return DOBBY_ERR_FAILED; }
    unsigned short *h = (unsigned short *)fn;
    h[0] = 0xF8DF; h[1] = 0x0000;              // ldr.w pc, [pc, #0]
    *(unsigned int *)(fn + 4) = (unsigned int)((uintptr_t)replace | 1);
    Util::Flush((void *)fn, HEAD);

    Stealth::SnapshotIntegrity(target, *(unsigned int *)tramp);

    HookSlot &s = g_slots[g_slot_count++];
    s.target = target; s.replace = replace; s.trampoline = tramp; s.head_len = HEAD;
    if (origin) *origin = (void *)((uintptr_t)tramp | 1);
    DOBBY_LOG_I("hooked %p -> %p (trampoline %p, thumb)", target, replace, tramp);
    EmitEvent(target, replace, origin ? *origin : nullptr, nullptr, nullptr, DOBBY_OK);
    return DOBBY_OK;
}

int Hooker::Unhook(void *target) {
    int idx = FindSlot(target);
    if (idx < 0) { DOBBY_LOG_W("unhook: slot not found %p", target); return DOBBY_ERR_NOT_FOUND; }
    HookSlot &s = g_slots[idx];
    uintptr_t fn = (uintptr_t)target & ~1;
    // 还原原指令: 从 trampoline 复制回头 8B
    Util::Write((void *)fn, s.trampoline, 8);
    munmap(s.trampoline, 0x1000);
    g_slots[idx] = g_slots[--g_slot_count];
    DOBBY_LOG_I("unhooked %p (trampoline released)", target);
    EmitEvent(target, nullptr, nullptr, nullptr, nullptr, DOBBY_OK);
    return DOBBY_OK;
}

bool Hooker::IsHooked(void *target) { return FindSlot(target) >= 0; }
int Hooker::Count() { return g_slot_count; }

} // namespace dobby
