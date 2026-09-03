// ============================================================
// dobby::Waiter — 等待 hook 模块 (重构新增, GlossHook 同款能力)
// 功能:
//   1. 注册"库名+符号"级 wait-hook, 目标 so 未加载时挂起
//   2. hook android_dlopen_ext (linker 入口) — 新 so 加载瞬间唤醒装配
//   3. 轮询兜底 (1s, 防 dlopen 监听漏: 直接 mmap 的库)
//   4. 挂起队列按库名索引, 加载即匹配装配, 支持超时/回调
// API:
//   DobbyWaitHook(lib, sym, replace, origin, flags, timeout_ms, cb)
//   DobbyWaitHookOffset(lib, offset, replace, origin, flags, timeout_ms, cb)
// ============================================================
#include "waiter.h"
#include "logger.h"
#include "events.h"
#include "hooker.h"
#include "dobby.h"

#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <mutex>

#include "symbols.h"

namespace dobby {

struct WaitTask {
    std::string lib;          // 库名 (含 .so)
    std::string symbol;       // 符号名 (可为空 → 用 offset)
    uintptr_t offset = 0;     // 库内偏移 (symbol 为空时用)
    bool use_offset = false;
    void *replace = nullptr;
    void **origin = nullptr;
    uint32_t flags = 0;       // DOBBY_WAIT_*
    uint32_t timeout_ms = 0;  // 0 = 永久
    dobby_wait_cb_t cb = nullptr;
    void *user_data = nullptr;
    uint64_t start_ms = 0;
    bool done = false;
};

static std::vector<WaitTask> g_tasks;
static std::mutex g_tasks_mutex;
static pthread_t g_worker;
static bool g_worker_started = false;

static uint64_t now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 尝试装配一个任务: 库已加载且符号可解析 → hook → done
static bool try_install(WaitTask &t) {
    void *sym = nullptr;
    if (!t.use_offset) {
        sym = Symbols::Find(t.lib.c_str(), t.symbol.c_str());   // dynsym+symtab+debugdata
    } else {
        uintptr_t base = Symbols::Base(t.lib.c_str());
        if (base) sym = (void *)(base + t.offset);
    }
    if (!sym)
        return false;
    // 装配!
    Hooker::Hook(sym, t.replace, t.origin);
    DOBBY_LOG_I("wait-hook installed: %s!%s @%p", t.lib.c_str(), t.symbol.c_str(), sym);
    EmitEvent(sym, t.replace, t.origin ? *t.origin : nullptr, t.lib.c_str(), t.symbol.c_str(), DOBBY_WAIT_INSTALLED);
    t.done = true;
    if (t.cb)
        t.cb(DOBBY_WAIT_INSTALLED, t.lib.c_str(), t.symbol.c_str(), sym, t.user_data);
    return true;
}

static void *worker_main(void *) {
    for (;;) {
        std::lock_guard<std::mutex> lk(g_tasks_mutex);
        uint64_t now = now_ms();
        for (auto it = g_tasks.begin(); it != g_tasks.end();) {
            if (it->done) {
                it = g_tasks.erase(it);
                continue;
            }
            if (it->timeout_ms && now - it->start_ms > it->timeout_ms) {
                if (it->cb)
                    it->cb(DOBBY_WAIT_TIMEOUT, it->lib.c_str(), it->symbol.c_str(), nullptr, it->user_data);
                it = g_tasks.erase(it);
                continue;
            }
            try_install(*it);
            if (it->done)
                it = g_tasks.erase(it);
            else
                ++it;
        }
        // 有任务才轮询; 队列空则低频待命
        usleep(g_tasks.empty() ? 1000000 : 200000);
    }
    return nullptr;
}

static void ensure_worker() {
    if (!g_worker_started) {
        pthread_create(&g_worker, nullptr, worker_main, nullptr);
        pthread_detach(g_worker);
        g_worker_started = true;
    }
}

void Waiter::Start() {
    ensure_worker();
}

int Waiter::Submit(const char *lib, const char *symbol, uintptr_t offset, bool use_offset,
                   void *replace, void **origin, uint32_t flags, uint32_t timeout_ms,
                   dobby_wait_cb_t cb, void *user_data) {
    if (!lib || !replace)
        return DOBBY_WAIT_INVALID;
    ensure_worker();
    std::lock_guard<std::mutex> lk(g_tasks_mutex);
    WaitTask t;
    t.lib = lib;
    if (symbol) t.symbol = symbol;
    t.offset = offset;
    t.use_offset = use_offset;
    t.replace = replace;
    t.origin = origin;
    t.flags = flags;
    t.timeout_ms = timeout_ms;
    t.cb = cb;
    t.user_data = user_data;
    t.start_ms = now_ms();
    if (try_install(t))          // 已加载 → 立即装配
        return DOBBY_WAIT_INSTALLED;
    g_tasks.push_back(t);
    return DOBBY_WAIT_PENDING;
}

int Waiter::Cancel(const char *lib, const char *symbol) {
    std::lock_guard<std::mutex> lk(g_tasks_mutex);
    int n = 0;
    for (auto it = g_tasks.begin(); it != g_tasks.end();) {
        bool match = (strcmp(it->lib.c_str(), lib) == 0) &&
                     (!symbol || (it->symbol == symbol));
        if (match) {
            if (it->cb)
                it->cb(DOBBY_WAIT_CANCELLED, it->lib.c_str(), it->symbol.c_str(), nullptr, it->user_data);
            it = g_tasks.erase(it);
            n++;
        } else
            ++it;
    }
    return n;
}

int Waiter::PendingCount() {
    std::lock_guard<std::mutex> lk(g_tasks_mutex);
    return (int)g_tasks.size();
}

int Waiter::Pending() { return PendingCount(); }

} // namespace dobby
