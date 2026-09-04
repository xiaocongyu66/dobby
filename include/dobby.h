// ============================================================
// Dobby v2 (xyc fork 重构版) — 统一公开 API
// 模块: hooker (inline) / plt_hooker (PLT/GOT) / waiter (等待 hook)
//       stealth (反检测) / symbols (xDL) / util (内存工具)
// ============================================================
#ifndef DOBBY_H
#define DOBBY_H

// ===================== 使用示例 =====================
// ============================================================
//
// 【1. 基础 inline hook】
//   static int (*orig_add)(int, int);
//   int my_add(int a, int b) { return orig_add(a, b) + 1; }
//   DobbyInit(false);
//   DobbyHook((void*)add_addr, (void*)my_add, (void**)&orig_add);
//   DobbyUnhook((void*)add_addr);          // 解除
//
// 【2. 符号 hook (自动解析 dynsym+symtab+gnu_debugdata)】
//   void *p = DobbySymbol("libMSDK.so", "exit");
//   uintptr_t base = DobbyBase("libMSDK.so");
//   DobbyAndroidHookSymbol("libMSDK.so", "exit", my_exit, &orig_exit);
//
// 【3. PLT/GOT hook (对指定库的符号调用点生效)】
//   DobbyPltHook("libtprt.so", "open", my_open, &orig_open);
//   DobbyPltUnhook("libtprt.so", "open");
//
// 【4. 等待 hook (目标库未加载时自动挂起, 加载瞬间装配 — GlossHook 同款)】
//   DobbyWaitHook("libMSDK.so", "exit", my_exit, &orig_exit,
//                 10000, on_hooked, nullptr);        // 10s 超时
//   DobbyWaitHookOffset("libtprt.so", 0x18c6c, patch_fn, &orig,
//                       0, on_hooked, nullptr);      // 永久等待
//
// 【5. 高级自定义 (DobbyOptions — 多参数高度自定义)】
//   DobbyOptions opt = {};
//   opt.flags = DOBBY_HOOK_DISGUISE_PAGE      // trampoline 页改名伪装
//             | DOBBY_HOOK_VERIFY;            // 安装后快照完整性
//   opt.jump_encoding = DOBBY_JUMP_LDR;       // 跳转编码
//   opt.head_len_override = 8;                // 自定义函数头覆盖长度
//   opt.timeout_ms = 5000;                    // wait 超时
//   opt.cb = on_hooked; opt.user = nullptr;
//   DobbyHookEx2((void*)target, my_fn, &orig, &opt);
//   DobbyWaitEx("libMSDK.so", "exit", 0, false, my_exit, &orig, &opt);
//
// 【6. 内存补丁】
//   DobbyMemoryNop(addr, 4);                  // NOP (thumb/arm 自适应)
//   DobbyMemoryWrite(addr, bytes, sizeof(bytes));
//   DobbyMemoryProtect(addr, 0x1000, PROT_READ|PROT_WRITE);
//
// 【7. 反检测】
//   DobbyStealthDisguise(page, 0x1000);       // 匿名页伪装系统段名
//   bool ok = DobbyStealthVerify(addr);       // hook 完整性 (TP unhook 检测)
//
// 【8. 多 hook 共存/事务式管理】
//   // 同一位置可多次 hook; GlossHook/DeleteAll 按地址管理 (gloss_hook_manager)
// ============================================================


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
// 正则批量 PLT hook (xHook 同款): 对所有匹配 path_regex 的 so 的 symbol 槽替换
// 例: DobbyPltHookRegex(".*\\.so$", "exit", my_exit, &orig);
int DobbyPltHookRegex(const char *path_regex, const char *symbol,
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


// ---------- 调用点 Hook (hook 函数内单个 BL/BLX 调用点) ----------
// site: BL/BLX 指令地址 (thumb: bit0=1). orig_callee 收原 callee.
int DobbyCallSiteHook(void *site, void **orig_callee, void *new_callee);
int DobbyCallSiteUnhook(void *site);

// ---------- .init_array 枚举 (so 构造函数) ----------
int DobbyReadInitArray(const char *lib_name, uintptr_t *out, int max);


// ---------- Hook 事件回调 (每次装配成功/失败通知宿主 — GlossHook 同款) ----------
typedef struct {
  void *target;        // hook 目标
  void *replace;       // 替换函数
  void *origin;        // 原函数 (可用时)
  const char *lib;     // 库名 (plt/wait 场景)
  const char *symbol;  // 符号名 (plt/wait 场景)
  int status;          // DOBBY_OK / DOBBY_ERR_*
} DobbyHookEvent;
typedef void (*dobby_event_cb_t)(const DobbyHookEvent *ev);
void DobbyOnHookEvent(dobby_event_cb_t cb);

// ---------- 软件断点 (SIGTRAP 指令级观察) ----------
int DobbyBreakpointInstall(void *addr, void (*cb)(void *ctx, void *user), void *user);
int DobbyBreakpointReArm(void *addr);
int DobbyBreakpointRemove(void *addr);

// ---------- 槽位守卫 (周期验证+自动恢复 — 防完整性引擎恢复槽) ----------
int DobbyGuardAdd(void *slot, void *expected_value);
int DobbyGuardRemove(void *slot);

// ---------- 反检测 (stealth) ----------
// trampoline 匿名页改名伪装 (系统段名池)
int DobbyStealthDisguise(void *page, size_t size);
// hook 点完整性校验 (true=未被 unhook)
bool DobbyStealthVerify(void *hooked_addr);


#ifdef __cplusplus
}
#endif
#endif // DOBBY_H
