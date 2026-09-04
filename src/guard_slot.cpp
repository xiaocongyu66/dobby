// ============================================================
// dobby::GuardSlot — 通用槽位守卫 (周期验证 + 自动恢复)
// 场景: 任何被替换的 GOT 槽 / 函数头 / 数据槽, 被完整性引擎恢复时自动重写.
// API:
//   DobbyGuardAdd(slot_addr, expected_value, rewrite) — 守卫一个槽
//   DobbyGuardRun() — 已由内部线程周期执行
// ============================================================
#include "guard_slot.h"
#include "logger.h"
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <vector>

namespace dobby {

struct GuardSlotRec {
    void *addr;             // 槽地址
    void *expected;         // 期望值 (我们写的)
    bool enabled;
};
static std::vector<GuardSlotRec> g_guards;
static pthread_mutex_t g_guard_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_guard_started = false;

int GuardSlot::Add(void *slot, void *expected_value) {
    pthread_mutex_lock(&g_guard_mutex);
    GuardSlotRec r = {slot, expected_value, true};
    g_guards.push_back(r);
    pthread_mutex_unlock(&g_guard_mutex);
    return DOBBY_OK;
}

int GuardSlot::Remove(void *slot) {
    pthread_mutex_lock(&g_guard_mutex);
    for (auto it = g_guards.begin(); it != g_guards.end(); ++it) {
        if (it->addr == slot) { g_guards.erase(it); pthread_mutex_unlock(&g_guard_mutex); return DOBBY_OK; }
    }
    pthread_mutex_unlock(&g_guard_mutex);
    return DOBBY_ERR_NOT_FOUND;
}

static void *guard_worker(void *) {
    pthread_setname_np(pthread_self(), "guard_worker");
    for (;;) {
        sleep(1);
        pthread_mutex_lock(&g_guard_mutex);
        for (auto &r : g_guards) {
            if (!r.enabled) continue;
            void *cur = *(void **)r.addr;
            if (cur != r.expected) {
                void *was = cur;
                *(void **)r.addr = r.expected;
                DOBBY_LOG_I("guard: slot %p restored (%p -> %p)", r.addr, was, r.expected);
            }
        }
        pthread_mutex_unlock(&g_guard_mutex);
    }
    return nullptr;
}

void GuardSlot::Start() {
    pthread_mutex_lock(&g_guard_mutex);
    if (!g_guard_started) {
        pthread_t t; pthread_create(&t, nullptr, guard_worker, nullptr); pthread_detach(t);
        g_guard_started = true;
    }
    pthread_mutex_unlock(&g_guard_mutex);
}

} // namespace dobby
