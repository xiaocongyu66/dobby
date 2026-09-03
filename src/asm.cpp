// ============================================================
// dobby::Asm — Keystone 汇编引擎封装 (运行时汇编生成)
// 能力: 文本汇编 → 机器码; 一行式 patch; trampoline 跳转序列生成
// ============================================================
#include "asm.h"
#include "util.h"
#include "logger.h"
#include <keystone.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>

namespace dobby {

static ks_engine *g_ks_thumb = nullptr;
static ks_engine *g_ks_arm = nullptr;
static std::mutex g_ks_mutex;

static bool ensure_engine(bool thumb) {
    std::lock_guard<std::mutex> lk(g_ks_mutex);
    ks_engine *&e = thumb ? g_ks_thumb : g_ks_arm;
    if (e) return true;
    ks_arch arch = KS_ARCH_ARM;
    ks_mode mode = thumb ? (ks_mode)(KS_MODE_THUMB | KS_MODE_LITTLE_ENDIAN)
                         : (ks_mode)(KS_MODE_ARM | KS_MODE_LITTLE_ENDIAN);
    if (ks_open(arch, mode, &e) != KS_ERR_OK) { e = nullptr; return false; }
    ks_option(e, KS_OPT_SYNTAX, KS_OPT_SYNTAX_GAS);
    return true;
}

// 汇编文本 → 机器码 (thumb: addr bit0). 失败返回 false.
bool Asm::Assemble(const char *code, uintptr_t addr, unsigned char *out, size_t *out_size) {
    if (!code || !out || !out_size) return false;
    bool thumb = addr & 1;
    if (!ensure_engine(thumb)) return false;
    ks_engine *ks = thumb ? g_ks_thumb : g_ks_arm;
    std::lock_guard<std::mutex> lk(g_ks_mutex);
    size_t size = 0, count = 0;
    unsigned char *enc = nullptr;
    // Keystone 地址设为目标 (PC-rel 汇编需正确基址):
    ks_option(ks, KS_OPT_ADDR, thumb ? (addr & ~1) : addr);
    if (ks_asm(ks, code, thumb ? (addr & ~1) : addr, &enc, &size, &count) != KS_ERR_OK) {
        DOBBY_LOG_E("ks_asm failed: %s", code);
        return false;
    }
    if (size > *out_size) { ks_free(enc); return false; }
    memcpy(out, enc, size);
    *out_size = size;
    ks_free(enc);
    return true;
}

// 一行式 patch: 汇编代码直接写入 addr (跨页安全)
int Asm::PatchAsm(const char *code, void *addr) {
    unsigned char buf[256];
    size_t sz = sizeof(buf);
    if (!Assemble(code, (uintptr_t)addr, buf, &sz)) return DOBBY_ERR_FAILED;
    return Util::Write(addr, buf, sz);
}

} // namespace dobby
