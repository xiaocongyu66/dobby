// ============================================================
// dobby::Logger — 统一日志 (GlossHook 同款: 全局开关+统一Tag+分级)
// ============================================================
#ifndef DOBBY_LOGGER_H
#define DOBBY_LOGGER_H
#include <stdbool.h>
#if defined(__ANDROID__)
#include <android/log.h>
#else
// 本机验证 stub — NDK 上走真 logd:
enum { ANDROID_LOG_DEBUG = 3, ANDROID_LOG_INFO = 4, ANDROID_LOG_WARN = 5, ANDROID_LOG_ERROR = 6 };
static inline int __android_log_print(int, const char *, const char *, ...) { return 0; }
#endif

#define DOBBY_LOG_TAG "Dobby"

typedef enum {
    DOBBY_LOG_DEBUG = 0,
    DOBBY_LOG_INFO,
    DOBBY_LOG_WARN,
    DOBBY_LOG_ERROR,
} DobbyLogLevel;

#ifdef __cplusplus
extern "C" {
#endif

// 全局日志开关 (默认关 — 生产零日志串)
void DobbyEnableLog(bool enable);
// 分级阈值 (默认 ERROR: 只有错误才在 enable 后输出)
void DobbyLogLevelSet(DobbyLogLevel level);

#define DOBBY_LOG_D(...) do { if (dobby_log_enabled() && dobby_log_level() <= DOBBY_LOG_DEBUG) __android_log_print(ANDROID_LOG_DEBUG, DOBBY_LOG_TAG, __VA_ARGS__); } while (0)
#define DOBBY_LOG_I(...) do { if (dobby_log_enabled() && dobby_log_level() <= DOBBY_LOG_INFO)  __android_log_print(ANDROID_LOG_INFO,  DOBBY_LOG_TAG, __VA_ARGS__); } while (0)
#define DOBBY_LOG_W(...) do { if (dobby_log_enabled() && dobby_log_level() <= DOBBY_LOG_WARN)  __android_log_print(ANDROID_LOG_WARN,  DOBBY_LOG_TAG, __VA_ARGS__); } while (0)
#define DOBBY_LOG_E(...) do { if (dobby_log_enabled() && dobby_log_level() <= DOBBY_LOG_ERROR) __android_log_print(ANDROID_LOG_ERROR, DOBBY_LOG_TAG, __VA_ARGS__); } while (0)

// 内部:
bool dobby_log_enabled(void);
DobbyLogLevel dobby_log_level(void);

#ifdef __cplusplus
}
#endif
#endif
