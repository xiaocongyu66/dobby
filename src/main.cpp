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
#include "linker_monitor.h"
#include "breakpoint.h"
#include "dobby_instrument.h"
#include "plt_regex.h"
#include "callsite.h"
#include "initarray.h"
#include <cstdio>

static bool g_init = false;

extern "C" {

int DobbyInit(bool is_init_linker) {
    if (g_init) return DOBBY_OK;
    dobby::PltHooker::Start();
    dobby::Waiter::Start();
    dobby::LinkerMonitor::Start();
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

int DobbyCallSiteHook(void *site, void **orig_callee, void *new_callee) {
    return dobby::CallSite::Hook(site, orig_callee, new_callee);
}
int DobbyCallSiteUnhook(void *site) { return dobby::CallSite::Unhook(site); }
int DobbyReadInitArray(const char *lib_name, uintptr_t *out, int max) {
    return dobby::InitArray::Read(lib_name, out, max);
}

int DobbyPltHookRegex(const char *path_regex, const char *symbol,
                      void *replace, void **origin) {
    if (!g_init) DobbyInit(false);
    return dobby::PltRegex::HookRegex(path_regex, symbol, replace, origin);
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

void *DobbyIntercept(const char *lib, const char *symbol,
                     void (*interceptor)(DobbyRegContext *, void *),
                     void *user, uint32_t flags) {
    return dobby::Instrument::Intercept(lib, symbol, interceptor, user, flags);
}
void *DobbyInterceptAddr(void *addr,
                         void (*interceptor)(DobbyRegContext *, void *),
                         void *user, uint32_t flags) {
    return dobby::Instrument::InterceptAddr(addr, interceptor, user, flags);
}
int DobbyUnintercept(void *stub) { return dobby::Instrument::Unintercept(stub); }
bool DobbyInstrumentStatsGet(void *stub, DobbyInstrumentStats *out) {
    return dobby::Instrument::StatsGet(stub, out);
}
int DobbyPatchInstrument(void *addr, const void *patch, size_t len, void *origin_backup) {
    return dobby::Instrument::Patch(addr, patch, len, origin_backup);
}
int DobbyPatchRestore(void *addr, const void *original, size_t len) {
    return dobby::Instrument::PatchRestore(addr, original, len);
}

int DobbyBreakpointInstall(void *addr, void (*cb)(void *ctx, void *user), void *user) {
    return dobby::Breakpoint::Install(addr, (void (*)(DobbyRegContext *, void *))cb, user);
}
int DobbyBreakpointReArm(void *addr) { return dobby::Breakpoint::ReArm(addr); }
int DobbyBreakpointRemove(void *addr) { return dobby::Breakpoint::Remove(addr); }

int DobbyStealthDisguise(void *page, size_t size) { return dobby::Stealth::DisguisePage(page, size) ? 0 : -1; }
bool DobbyStealthVerify(void *hooked_addr) { return dobby::Stealth::VerifyIntegrity(hooked_addr); }

} // extern "C"
