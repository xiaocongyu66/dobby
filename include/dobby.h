// ============================================================
// Dobby v2 (xyc fork 重构版) — 统一公开 API
// 模块: hooker (inline) / plt_hooker (PLT/GOT) / waiter (等待 hook)
//       stealth (反检测) / symbols (xDL) / util (内存工具)
// ============================================================
#ifndef DOBBY_H
#define DOBBY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------- 版本 ----------
#define DOBBY_VERSION "2.0.0-xycrebuild"

// ---------- 状态 ----------
typedef enum {
  DOBBY_OK = 0,
  DOBBY_ERR_INVALID = -1,
  DOBBY_ERR_NOT_FOUND = -2,
  DOBBY_ERR_ALREADY = -3,
  DOBBY_ERR_FAILED = -4,
  DOBBY_ERR_UNSUPPORTED = -5,
} DobbyStatus;

// ---------- 生命周期 ----------
// is_init_linker: 是否同时 hook linker (dlopen/dlsym 监听). 常规 false.
int DobbyInit(bool is_init_linker);
void DobbyShutdown(void);
const char *DobbyVersion(void);

// ---------- Inline Hook ----------
// 函数头 hook. origin 收到可回调的原函数跳板.
int DobbyHook(void *target, void *replace, void **origin);
// 函数内任意地址 hook (跳板可选)
int DobbyHookAddr(void *func_addr, void *replace, void **origin);
// 解除 (按地址, 该地址全部 hook)
int DobbyUnhook(void *address);

// ---------- PLT/GOT Hook ----------
// 对指定库的 PLT/GOT 表中 symbol 槽全部替换 (支持运行时解析)
int DobbyPltHook(const char *lib_name, const char *symbol,
                 void *replace, void **origin);
// 撤销某库某符号的 PLT hook
int DobbyPltUnhook(const char *lib_name, const char *symbol);

// ---------- 等待 Hook (GlossHook 同款: 目标未加载自动挂起, 加载即装) ----------
#define DOBBY_WAIT_PENDING     0
#define DOBBY_WAIT_INSTALLED   1
#define DOBBY_WAIT_TIMEOUT    -2
#define DOBBY_WAIT_CANCELLED  -3
#define DOBBY_WAIT_INVALID    -4

typedef void (*dobby_wait_cb_t)(int status, const char *lib, const char *symbol,
                                void *resolved, void *user);

int DobbyWaitHook(const char *lib_name, const char *symbol,
                  void *replace, void **origin,
                  uint32_t timeout_ms, dobby_wait_cb_t cb, void *user);
int DobbyWaitHookOffset(const char *lib_name, uintptr_t offset,
                        void *replace, void **origin,
                        uint32_t timeout_ms, dobby_wait_cb_t cb, void *user);
int DobbyWaitCancel(const char *lib_name, const char *symbol);
int DobbyWaitPending(void);

// ---------- 符号解析 (xDL 增强版) ----------
// dynsym + symtab + gnu_debugdata, 绕 linker namespace
void *DobbySymbol(const char *lib_name, const char *symbol);
uintptr_t DobbyBase(const char *lib_name);

// ---------- 内存工具 ----------
int DobbyMemoryProtect(void *addr, size_t len, int prot);
int DobbyMemoryWrite(void *addr, const void *buf, size_t len);
int DobbyMemoryRead(void *buf, const void *addr, size_t len);
void DobbyMemoryNop(void *addr, size_t len);   // NOP 填充 (thumb/arm 自适应)
void DobbyMemoryFlush(void *addr, size_t len); // icache 清理

// ---------- 反检测 (stealth) ----------
// trampoline 匿名页改名伪装 (系统段名池)
int DobbyStealthDisguise(void *page, size_t size);
// hook 点完整性校验 (true=未被 unhook)
bool DobbyStealthVerify(void *hooked_addr);

#ifdef __cplusplus
}
#endif
#endif // DOBBY_H
