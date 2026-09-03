// ============================================================
// dobby::CallSite — 单调用点 hook (GlossHook 同款能力)
// 用途: 只拦截某函数的"某一次调用" (其它调用点不受影响)
// 实现: 找到调用点 BL 指令 → 改写为 BL stub → stub 里可回调原 callee 或改参
// ============================================================
#include "callsite.h"
#include "util.h"
#include "stealth.h"
#include <string.h>
#include <sys/mman.h>

namespace dobby {

// 调用点记录
struct CsRec {
    void *site;        // BL 指令地址 (thumb: bit0=1)
    void *orig_callee; // 原 callee
    void *stub;        // 我们的 stub
};
static CsRec g_sites[32];
static int g_cs_count = 0;

// thumb BL 指令改写: 保持编码格式, 修正目标为 new_callee
static bool rewrite_thumb_bl(void *site, void *new_callee) {
    uintptr_t a = (uintptr_t)site;
    bool thumb = a & 1;
    uintptr_t base = thumb ? a - 1 : a;
    unsigned short *h = (unsigned short *)base;
    unsigned hw = h[0], hw2 = h[1];
    if ((hw & 0xF800) != 0xF000) return false;              // 不是 BL/BLX 前半
    unsigned kind = hw2 & 0x2800;                            // 0x0000=BL 0x2800=BLX
    if (kind != 0x0000 && kind != 0x2800) return false;
    bool is_blx = (kind == 0x2800);
    // 重算 offset (thumb BL ±16MB):
    int32_t off = (int32_t)((uintptr_t)new_callee - (base + 4));
    if (is_blx) off &= ~3;
    if (off > 0x7FFFFF || off < -(int32_t)0x800000) return false;
    unsigned S = (off >> 24) & 1;
    unsigned I1 = (off >> 23) & 1, I2 = (off >> 22) & 1;
    unsigned imm10 = (off >> 12) & 0x3FF, imm11 = (off >> 1) & 0x7FF;
    // thumb 编码 I1=~J1^S...
    unsigned J1 = 1 - (I1 ^ S), J2 = 1 - (I2 ^ S);
    h[0] = 0xF000 | (S << 10) | imm10;
    h[1] = (is_blx ? 0xC000 : 0xD000) | (J1 << 13) | (J2 << 11) | imm11;
    Util::Flush(site, 4);
    return true;
}

int CallSite::Hook(void *site, void **orig_callee_out, void *new_callee) {
    if (!site || !new_callee) return DOBBY_ERR_INVALID;
    uintptr_t a = (uintptr_t)site;
    bool thumb = a & 1;
    if (!thumb) return DOBBY_ERR_UNSUPPORTED;   // arm callsite 后续
    uintptr_t base = a - 1;
    unsigned short *h = (unsigned short *)base;
    // 解析当前 BL 目标:
    unsigned hw = h[0], hw2 = h[1];
    if ((hw & 0xF800) != 0xF000) return DOBBY_ERR_UNSUPPORTED;
    bool is_blx = (hw2 & 0x2800) == 0x2800;
    unsigned S = (hw >> 10) & 1, imm10 = hw & 0x3FF;
    unsigned J1 = (hw2 >> 13) & 1, J2 = (hw2 >> 11) & 1, imm11 = hw2 & 0x7FF;
    unsigned I1 = 1 - (J1 ^ S), I2 = 1 - (J2 ^ S);
    int32_t off = (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | (imm11 << 1);
    if (off & 0x800000) off -= 1 << 24;
    void *callee = (void *)(base + 4 + off);
    if (is_blx) callee = (void *)((uintptr_t)callee & ~3);
    if (orig_callee_out) *orig_callee_out = callee;

    if (g_cs_count >= (int)(sizeof(g_sites)/sizeof(g_sites[0]))) return DOBBY_ERR_FAILED;
    if (Util::Protect(site, 4, PROT_READ|PROT_WRITE|PROT_EXEC) != 0) return DOBBY_ERR_FAILED;
    if (!rewrite_thumb_bl(site, new_callee)) return DOBBY_ERR_FAILED;

    g_sites[g_cs_count++] = {site, callee, nullptr};
    return DOBBY_OK;
}

int CallSite::Unhook(void *site) {
    for (int i = 0; i < g_cs_count; i++) {
        if (g_sites[i].site == site) {
            rewrite_thumb_bl(site, g_sites[i].orig_callee);   // 还原
            g_sites[i] = g_sites[--g_cs_count];
            return DOBBY_OK;
        }
    }
    return DOBBY_ERR_NOT_FOUND;
}

void *CallSite::OrigCallee(void *site) {
    for (int i = 0; i < g_cs_count; i++)
        if (g_sites[i].site == site) return g_sites[i].orig_callee;
    return nullptr;
}

} // namespace dobby
