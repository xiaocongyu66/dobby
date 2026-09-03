// ============================================================
// dobby::Symbols — xDL 2.4 封装 (dynsym+symtab+gnu_debugdata)
// ============================================================
#include "symbols.h"
#if defined(__ANDROID__)
extern "C" {
#include "xdl.h"
}
#else
// 本机 (非 Android) 验证 stub — NDK 上走真 xDL:
#include <stddef.h>
extern "C" {
static void *xdl_open(const char *, int) { return nullptr; }
static void *xdl_sym(void *, const char *, size_t *) { return nullptr; }
static void *xdl_dsym(void *, const char *, size_t *) { return nullptr; }
static void xdl_close(void *) {}
#define XDL_DEFAULT 0
}
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace dobby {

void *Symbols::Find(const char *lib, const char *sym) {
    if (!lib || !sym) return nullptr;
    void *h = xdl_open(lib, XDL_DEFAULT);
    if (!h) return nullptr;
    void *p = xdl_sym(h, sym, nullptr);
    if (!p) p = xdl_dsym(h, sym, nullptr);
    xdl_close(h);
    return p;
}

uintptr_t Symbols::Base(const char *lib) {
    if (!lib) return 0;
    // xDL 无 bias API — 用 /proc/self/maps 解析基址:
    char needle[128];
    snprintf(needle, sizeof(needle), " %s", lib);
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        char *dash = strstr(line, "-");
        const char *nm = strrchr(line, '/');
        if (!dash || !nm || !strstr(nm, lib)) continue;
        base = strtoul(line, nullptr, 16);
        break;
    }
    fclose(f);
    return base;
}

} // namespace dobby
