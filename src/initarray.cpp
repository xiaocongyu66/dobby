// ============================================================
// dobby::InitArray — so 构造函数(.init_array)拦截
// 用途: TP 组件 (tprt/tersafe) 的解密器都在 .init_array — 加载瞬间拦截!
// 机制: hook linker 内部 __dl_call_array (fork BionicLinkerUtil 的
//       resolve_elf_internal_symbol 能力) — 全局拦截 init 执行.
// 降级: __dl_call_array 不可用时, 本模块仅提供枚举 (init_array 内容读取).
// ============================================================
#include "initarray.h"
#include "symbols.h"
#include "util.h"
#include <string.h>
#include <elf.h>

namespace dobby {

// 读取已加载 so 的 .init_array 内容 (函数指针数组 + 数量)
int InitArray::Read(const char *lib, uintptr_t *out, int max) {
    // 从 maps 找基址 → 解析 ELF 段 → .init_array (PT_DYNAMIC DT_INIT_ARRAY)
    // 简化: 用 section header (so 加载后 section 不可用) — 用 PT_DYNAMIC:
    // 找基址:
    char needle[128];
    snprintf(needle, sizeof(needle), "/%s", lib);
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return -1;
    uintptr_t base = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, needle)) continue;
        base = strtoul(line, nullptr, 16);
        break;
    }
    fclose(f);
    if (!base) return -1;
    Elf32_Ehdr eh; memcpy(&eh, (void *)base, sizeof(eh));
    uintptr_t dyn = 0; size_t dynsz = 0;
    for (int i = 0; i < eh.e_phnum; i++) {
        Elf32_Phdr ph; memcpy(&ph, (void*)(base + eh.e_phoff + i*eh.e_phentsize), sizeof(ph));
        if (ph.p_type == PT_DYNAMIC) { dyn = base + ph.p_vaddr; dynsz = ph.p_memsz; break; }
    }
    if (!dyn) return -1;
    uintptr_t arr = 0; size_t cnt = 0;
    Elf32_Dyn *d = (Elf32_Dyn *)dyn;
    for (size_t i = 0; i < dynsz / sizeof(Elf32_Dyn); i++) {
        if (d[i].d_tag == DT_NULL) break;
        if (d[i].d_tag == DT_INIT_ARRAY) arr = d[i].d_un.d_ptr;
        if (d[i].d_tag == DT_INIT_ARRAYSZ) cnt = d[i].d_un.d_val / 4;
    }
    if (!arr) return -1;
    int n = 0;
    for (size_t i = 0; i < cnt && n < max; i++) {
        out[n++] = *(uintptr_t *)(arr + i * 4);
    }
    return n;
}

// linker 内部 call_array hook (TP 解密器拦截) — 需 BionicLinkerUtil, 下阶段接
int InitArray::HookLinkerCall(void *pre_cb, void *user) {
    (void)pre_cb; (void)user;
    return DOBBY_ERR_UNSUPPORTED;   // 阶段 2: 经 resolve_elf_internal_symbol 接入
}

} // namespace dobby
