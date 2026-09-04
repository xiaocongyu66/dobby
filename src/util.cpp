// ============================================================
// dobby::Util — 内存/权限/缓存 (ARM32 自适应 NOP/flush)
// ============================================================
#include "util.h"
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

namespace dobby {

int Util::Protect(void *addr, size_t len, int prot) {
    uintptr_t pg = (uintptr_t)addr & ~0xFFFUL;
    size_t plen = ((uintptr_t)addr + len - pg + 0xFFF) & ~0xFFFUL;
    return mprotect((void *)pg, plen, prot);
}

int Util::Write(void *addr, const void *buf, size_t len) {
    // ⚠️ 反检测: 不用 RWX (mprotect audit 抓 RWX 授予) — 
    // 写时 RW, 写后 RX (代码段原权限), 不带 X 的写入走 RW:
    if (Protect(addr, len, PROT_READ | PROT_WRITE) != 0) return -1;
    memcpy(addr, buf, len);
    Flush(addr, len);
    Protect(addr, len, PROT_READ | PROT_EXEC);   // 代码页恢复 RX (无 W)
    return 0;
}

int Util::Read(void *buf, const void *addr, size_t len) {
    memcpy(buf, addr, len);
    return 0;
}

// thumb NOP = 0x46C0 (2B), arm NOP = 0xE1A00000 (4B); 按 addr bit0 判 thumb
void Util::Nop(void *addr, size_t len) {
    uintptr_t a = (uintptr_t)addr;
    bool thumb = a & 1;
    uintptr_t base = thumb ? (a - 1) : a;
    if (thumb) {
        for (size_t i = 0; i + 2 <= len; i += 2) {
            unsigned short n = 0x46C0;
            Write((void *)(base + i), &n, 2);
        }
    } else {
        for (size_t i = 0; i + 4 <= len; i += 4) {
            unsigned int n = 0xE1A00000;
            Write((void *)(base + i), &n, 4);
        }
    }
}

void Util::Flush(void *addr, size_t len) {
#if defined(__arm__) || defined(__aarch64__)
    __builtin___clear_cache((char *)addr, (char *)addr + len);
#endif
}

} // namespace dobby
