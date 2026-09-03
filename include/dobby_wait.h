// ============================================================
// Dobby 等待 hook 公开 API (重构新增 — GlossHook 同款能力)
// 用法示例:
//   static void *orig_exit;
//   void my_exit(int c) { ((void(*)(int))orig_exit)(c); }
//   DobbyWaitHook("libMSDK.so", "exit", (void*)my_exit, &orig_exit,
//                 DOBBY_WAIT_FLAG_NONE, 0, my_cb, nullptr);
//   → libMSDK 未加载时挂起, 加载瞬间自动 hook; 已加载则立即 hook.
// ============================================================
#ifndef DOBBY_WAIT_API_H
#define DOBBY_WAIT_API_H
#include <stdint.h>

#define DOBBY_WAIT_STATUS_PENDING     0
#define DOBBY_WAIT_STATUS_INSTALLED   1
#define DOBBY_WAIT_STATUS_TIMEOUT    -2
#define DOBBY_WAIT_STATUS_CANCELLED  -3
#define DOBBY_WAIT_STATUS_INVALID    -4

typedef void (*dobby_wait_callback_t)(int status, const char *lib, const char *symbol,
                                      void *resolved_addr, void *user_data);

#ifdef __cplusplus
extern "C" {
#endif

// 等待 hook: 库未加载则挂起, 加载瞬间自动装配. timeout_ms=0 表示永久等待.
int DobbyWaitHook(const char *lib_name, const char *symbol_name,
                  void *replace, void **origin,
                  uint32_t flags, uint32_t timeout_ms,
                  dobby_wait_callback_t cb, void *user_data);

// 偏移版: 库加载后 hook 库基址+offset
int DobbyWaitHookOffset(const char *lib_name, uintptr_t offset,
                        void *replace, void **origin,
                        uint32_t flags, uint32_t timeout_ms,
                        dobby_wait_callback_t cb, void *user_data);

// 取消挂起的 wait-hook (返回取消数量)
int DobbyWaitCancel(const char *lib_name, const char *symbol_name);

// 当前挂起数量
int DobbyWaitPendingCount(void);

#ifdef __cplusplus
}
#endif
#endif // DOBBY_WAIT_API_H
