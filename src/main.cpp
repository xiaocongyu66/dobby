// ============================================================
// Dobby v2 main — 库入口 + 公开 API 分发
// ============================================================
#include "dobby.h"
#include "hooker.h"
#include "plt_hooker.h"
#include "waiter.h"
#include "symbols.h"
#include "util.h"
#include "stealth.h"
#include <cstdio>

static bool g_init = false;

extern "C" {

int DobbyInit(bool is_init_linker) {
    if (g_init) return DOBBY_OK;
    dobby::PltHooker::Start();
    dobby::Waiter::Start();
    g_init = true;
    return DOBBY_OK;
}

void DobbyShutdown(void) { g_init = false; }
const char *DobbyVersion(void) { return DOBBY_VERSION; }

int DobbyHook(void *target, void *replace, void **origin) {
    if (!g_init) DobbyInit(false);
    return dobby::Hooker::Hook(target, replace, origin);
}

int DobbyHookAddr(void *func_addr, void *replace, void **origin) {
    return DobbyHook(func_addr, replace, origin);
}

int DobbyUnhook(void *address) {
    return dobby::Hooker::Unhook(address);
}

int DobbyPltHook(const char *lib_name, const char *symbol,
                 void *replace, void **origin) {
    if (!g_init) DobbyInit(false);
    return dobby::PltHooker::Hook(lib_name, symbol, replace, origin);
}

int DobbyPltUnhook(const char *lib_name, const char *symbol) {
    return dobby::PltHooker::Unhook(lib_name, symbol);
}

int DobbyWaitHook(const char *lib_name, const char *symbol,
                  void *replace, void **origin,
                  uint32_t timeout_ms, dobby_wait_cb_t cb, void *user) {
    if (!g_init) DobbyInit(false);
    return dobby::Waiter::Submit(lib_name, symbol, 0, false, replace, origin,
                                 0, timeout_ms, cb, user);
}

int DobbyWaitHookOffset(const char *lib_name, uintptr_t offset,
                        void *replace, void **origin,
                        uint32_t timeout_ms, dobby_wait_cb_t cb, void *user) {
    if (!g_init) DobbyInit(false);
    return dobby::Waiter::Submit(lib_name, nullptr, offset, true, replace, origin,
                                 0, timeout_ms, cb, user);
}

int DobbyWaitCancel(const char *lib_name, const char *symbol) {
    return dobby::Waiter::Cancel(lib_name, symbol);
}

int DobbyWaitPending(void) { return dobby::Waiter::Pending(); }

void *DobbySymbol(const char *lib_name, const char *symbol) {
    return dobby::Symbols::Find(lib_name, symbol);
}

uintptr_t DobbyBase(const char *lib_name) { return dobby::Symbols::Base(lib_name); }

int DobbyMemoryProtect(void *addr, size_t len, int prot) { return dobby::Util::Protect(addr, len, prot); }
int DobbyMemoryWrite(void *addr, const void *buf, size_t len) { return dobby::Util::Write(addr, buf, len); }
int DobbyMemoryRead(void *buf, const void *addr, size_t len) { return dobby::Util::Read(buf, addr, len); }
void DobbyMemoryNop(void *addr, size_t len) { dobby::Util::Nop(addr, len); }
void DobbyMemoryFlush(void *addr, size_t len) { dobby::Util::Flush(addr, len); }

int DobbyStealthDisguise(void *page, size_t size) { return dobby::Stealth::DisguisePage(page, size) ? 0 : -1; }
bool DobbyStealthVerify(void *hooked_addr) { return dobby::Stealth::VerifyIntegrity(hooked_addr); }

} // extern "C"
