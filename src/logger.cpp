#include "logger.h"
#include <mutex>

static bool g_log_enabled = false;     // 默认关 (生产无日志串特征)
static DobbyLogLevel g_log_level = DOBBY_LOG_ERROR;

extern "C" {
void DobbyEnableLog(bool enable) {
    std::lock_guard<std::mutex> lk(*([]{ static std::mutex m; return &m; }()));
    g_log_enabled = enable;
}
void DobbyLogLevelSet(DobbyLogLevel level) {
    std::lock_guard<std::mutex> lk(*([]{ static std::mutex m; return &m; }()));
    g_log_level = level;
}
bool dobby_log_enabled(void) { return g_log_enabled; }
DobbyLogLevel dobby_log_level(void) { return g_log_level; }
}
