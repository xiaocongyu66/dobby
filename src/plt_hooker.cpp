// ============================================================
// dobby::PltHooker — PLT/GOT hook (重定位表驱动, 运行时解析每个库)
// ============================================================
#include "plt_hooker.h"
#include "dobby.h"
#include <cstdio>
#include <stdlib.h>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <elf.h>
#include "util.h"
#include <string.h>
#include <elf.h>
#include <pthread.h>

namespace dobby {

struct PltSlot { char lib[128]; char sym[128]; void *replace; void **origin; void *orig; };
static PltSlot g_plt[64];
static int g_plt_count = 0;

// 解析 lib 的 PT_DYNAMIC → 遍历 REL/PLTREL → 匹配 symbol → 替换槽
static int hook_one(uintptr_t base, PltSlot &s) {
    Elf32_Ehdr eh; memcpy(&eh, (void *)base, sizeof(eh));
    if (eh.e_ident[0] != 0x7f) return -1;
    uintptr_t dyn = 0; size_t dynsz = 0;
    for (int i = 0; i < eh.e_phnum; i++) {
        Elf32_Phdr ph; memcpy(&ph, (void*)(base + eh.e_phoff + i*eh.e_phentsize), sizeof(ph));
        if (ph.p_type == PT_DYNAMIC) { dyn = base + ph.p_vaddr; dynsz = ph.p_memsz; break; }
    }
    if (!dyn) return -1;
    uintptr_t symtab = 0, strtab = 0, jmprel = 0, rel = 0;
    size_t pltrelsz = 0, relsz = 0;
    Elf32_Dyn *dy = (Elf32_Dyn *)dyn;
    for (size_t i = 0; i < dynsz / sizeof(Elf32_Dyn); i++) {
        if (dy[i].d_tag == DT_NULL) break;
        switch (dy[i].d_tag) {
        case DT_SYMTAB: symtab = dy[i].d_un.d_ptr; break;
        case DT_STRTAB: strtab = dy[i].d_un.d_ptr; break;
        case DT_JMPREL: jmprel = dy[i].d_un.d_ptr; break;
        case DT_PLTRELSZ: pltrelsz = dy[i].d_un.d_val; break;
        case DT_REL: rel = dy[i].d_un.d_ptr; break;
        case DT_RELSZ: relsz = dy[i].d_un.d_val; break;
        default: break;
        }
    }
    int n = 0;
    auto scan = [&](uintptr_t rp, size_t sz) {
        if (!rp || !symtab || !strtab) return;
        Elf32_Rel *r = (Elf32_Rel *)rp;
        for (size_t j = 0; j < sz / sizeof(Elf32_Rel); j++) {
            unsigned idx = ELF32_R_SYM(r[j].r_info);
            if (!idx) continue;
            Elf32_Sym sy; memcpy(&sy, (void*)(symtab + idx*sizeof(Elf32_Sym)), sizeof(sy));
            const char *nm = (const char*)(strtab + sy.st_name);
            if (strcmp(nm, s.sym) == 0) {
                void **slot = (void **)r[j].r_offset;
                if (s.origin && *s.origin == nullptr) *s.origin = *slot;
                *slot = s.replace;
                n++;
            }
        }
    };
    Util::Protect((void *)base, 0x1000, PROT_READ|PROT_WRITE);
    scan(jmprel, pltrelsz);
    scan(rel, relsz);
    return n;
}

int PltHooker::Hook(const char *lib, const char *sym, void *replace, void **origin) {
    if (!lib || !sym || !replace) return DOBBY_ERR_INVALID;
    if (g_plt_count >= (int)(sizeof(g_plt)/sizeof(g_plt[0]))) return DOBBY_ERR_FAILED;
    PltSlot &s = g_plt[g_plt_count];
    snprintf(s.lib, sizeof(s.lib), "%s", lib);
    snprintf(s.sym, sizeof(s.sym), "%s", sym);
    s.replace = replace; s.origin = origin; s.orig = nullptr;
    g_plt_count++;
    return DOBBY_OK;   // 实际 hook 在 ApplyAll (遍历 maps) 执行
}

// 遍历 /proc/self/maps 对每个已加载库执行挂起的 PLT hook (含迟加载补挂)
static void *plt_worker(void *) {
    for (;;) {
        for (int i = 0; i < g_plt_count; i++) {
            PltSlot &s = g_plt[i];
            if (!s.replace) continue;   // 已撤
            FILE *f = fopen("/proc/self/maps", "r");
            if (!f) continue;
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                if (!strstr(line, s.lib)) continue;
                char *dash = strchr(line, '-');
                if (!dash) continue;
                *dash = 0;
                uintptr_t start = strtoul(line, nullptr, 16);
                hook_one(start, s);
                break;
            }
            fclose(f);
        }
        sleep(1);   // 1s 轮询 (lazy-binding/迟加载对抗)
    }
    return nullptr;
}

void PltHooker::Start() {
    static bool started = false;
    if (started) return;
    pthread_t t; pthread_create(&t, nullptr, plt_worker, nullptr); pthread_detach(t);
    started = true;
}

int PltHooker::Unhook(const char *lib, const char *sym) {
    for (int i = 0; i < g_plt_count; i++) {
        if (strcmp(g_plt[i].lib, lib) == 0 && strcmp(g_plt[i].sym, sym) == 0) {
            g_plt[i].replace = nullptr;   // worker 停止替换 (下一轮恢复 orig)
            return DOBBY_OK;
        }
    }
    return DOBBY_ERR_NOT_FOUND;
}

} // namespace dobby
