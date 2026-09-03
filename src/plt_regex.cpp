// ============================================================
// dobby::PltHooker 扩展 — 正则批量 hook (xHook 同款) + 多 hook 链
// ============================================================
#include "plt_regex.h"
#include <cstdio>
#include <cstring>
#include <stdlib.h>
#include <elf.h>
#include "util.h"
#include "logger.h"
#include <string.h>
#include <regex.h>       // POSIX regex (bionic 支持)
#include <pthread.h>

namespace dobby {

// ---- 正则批量注册 (xhook_register 同款) ----
struct RegexRule {
    char pattern[128];     // so 路径正则 (如 ".*\\.so$")
    char symbol[128];      // 符号名
    void *replace;
    void **origin;
};
static RegexRule g_rules[32];
static int g_rule_count = 0;

int PltRegex::HookRegex(const char *path_regex, const char *symbol,
                         void *replace, void **origin) {
    if (!path_regex || !symbol || !replace) return DOBBY_ERR_INVALID;
    if (g_rule_count >= (int)(sizeof(g_rules)/sizeof(g_rules[0]))) return DOBBY_ERR_FAILED;
    RegexRule &r = g_rules[g_rule_count++];
    snprintf(r.pattern, sizeof(r.pattern), "%s", path_regex);
    snprintf(r.symbol, sizeof(r.symbol), "%s", symbol);
    r.replace = replace; r.origin = origin;
    DOBBY_LOG_I("regex rule registered: /%s/ -> %s", path_regex, symbol);
    return DOBBY_OK;
}

// 在 plt_worker 的 maps 遍历里应用正则规则 (对每个 so 的每个匹配符号槽替换):
int PltRegex::ApplyRules(uintptr_t base, const char *path) {
    int total = 0;
    for (int i = 0; i < g_rule_count; i++) {
        RegexRule &rule = g_rules[i];
        regex_t re;
        if (regcomp(&re, rule.pattern, REG_EXTENDED | REG_NOSUB) != 0) continue;
        bool m = regexec(&re, path, 0, nullptr, 0) == 0;
        regfree(&re);
        if (!m) continue;
        // 解析这个 so 的重定位 → 匹配 rule.symbol → 替换
        Elf32_Ehdr eh; memcpy(&eh, (void *)base, sizeof(eh));
        if (eh.e_ident[0] != 0x7f) continue;
        uintptr_t dyn = 0; size_t dynsz = 0;
        for (int k = 0; k < eh.e_phnum; k++) {
            Elf32_Phdr ph; memcpy(&ph, (void*)(base + eh.e_phoff + k*eh.e_phentsize), sizeof(ph));
            if (ph.p_type == PT_DYNAMIC) { dyn = base + ph.p_vaddr; dynsz = ph.p_memsz; break; }
        }
        if (!dyn) continue;
        uintptr_t symtab = 0, strtab = 0, jmprel = 0;
        size_t pltrelsz = 0;
        Elf32_Dyn *dy = (Elf32_Dyn *)dyn;
        for (size_t j = 0; j < dynsz / sizeof(Elf32_Dyn); j++) {
            if (dy[j].d_tag == DT_NULL) break;
            if (dy[j].d_tag == DT_SYMTAB) symtab = dy[j].d_un.d_ptr;
            if (dy[j].d_tag == DT_STRTAB) strtab = dy[j].d_un.d_ptr;
            if (dy[j].d_tag == DT_JMPREL) jmprel = dy[j].d_un.d_ptr;
            if (dy[j].d_tag == DT_PLTRELSZ) pltrelsz = dy[j].d_un.d_val;
        }
        if (!jmprel || !symtab || !strtab) continue;
        Elf32_Rel *r2 = (Elf32_Rel *)jmprel;
        for (size_t j = 0; j < pltrelsz / sizeof(Elf32_Rel); j++) {
            unsigned idx = ELF32_R_SYM(r2[j].r_info);
            if (!idx) continue;
            Elf32_Sym sy; memcpy(&sy, (void*)(symtab + idx*sizeof(Elf32_Sym)), sizeof(sy));
            const char *nm = (const char*)(strtab + sy.st_name);
            if (strcmp(nm, rule.symbol) != 0) continue;
            void **slot = (void **)r2[j].r_offset;
            if (rule.origin && *rule.origin == nullptr) *rule.origin = *slot;
            DOBBY_LOG_I("regex plt: %s!%s slot %p -> %p", path, nm, slot, rule.replace);
            *slot = rule.replace;
            total++;
        }
    }
    return total;
}

// worker 循环里调用: maps 遍历 → apply_regex_rules
// (在 PltHooker::Start 的 plt_worker 中每轮追加此调用 — 见 plt_hooker.cpp)

} // namespace dobby
