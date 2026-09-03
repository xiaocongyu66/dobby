// ============================================================
// Dobby v2 — Android 便捷 API (老版本功能搬迁 1/3)
// ============================================================
#ifndef DOBBY_ANDROID_API_H
#define DOBBY_ANDROID_API_H
#include "dobby.h"

#define DOBBY_NAME_MAX 256

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char lib_name[DOBBY_NAME_MAX];
  char symbol_name[DOBBY_NAME_MAX];
  void *target;
  void *replace;
  void *origin;
  int enabled;
} DobbyHookRecord;

// 符号 hook (lib+sym → 自动解析 → inline hook)
int DobbyAndroidHookSymbol(const char *lib_name, const char *symbol_name,
                           void *replace, void **origin);
// 偏移 hook (lib base + offset)
int DobbyAndroidHookOffset(const char *lib_name, uintptr_t offset,
                           void *replace, void **origin);
// PLT hook (同 DobbyPltHook, 语义别名)
int DobbyAndroidHookPLT(const char *lib_name, const char *symbol_name,
                        void *replace, void **origin);
// hook 状态查询
int DobbyAndroidIsHooked(void *target);
// hook 数量
int DobbyAndroidHookCount(void);
// 等待 hook 挂起数量
int DobbyAndroidWaitPending(void);

#ifdef __cplusplus
}
#endif

#endif
