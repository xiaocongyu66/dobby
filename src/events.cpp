#include "events.h"
#include "logger.h"
#include <mutex>

static dobby_event_cb_t g_ev_cb = nullptr;
static std::mutex g_ev_mutex;

void DobbyOnHookEvent(dobby_event_cb_t cb) {
    std::lock_guard<std::mutex> lk(g_ev_mutex);
    g_ev_cb = cb;
}

void dobby::EmitEvent(void *target, void *replace, void *origin,
                      const char *lib, const char *symbol, int status) {
    std::lock_guard<std::mutex> lk(g_ev_mutex);
    if (!g_ev_cb) return;
    DobbyHookEvent ev = {target, replace, origin, lib, symbol, status};
    g_ev_cb(&ev);
    DOBBY_LOG_I("event: %s!%s @%p status=%d", lib ? lib : "-", symbol ? symbol : "-", target, status);
}
