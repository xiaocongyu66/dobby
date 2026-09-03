// ============================================================
// dobby::Instrument — 动态插桩 + 静态插桩 (Mem-Kit/ShadowHook 对齐)
// 动态: DobbyIntercept — 目标函数 pre-hook, 回调收完整寄存器上下文,
//       可读改参数/跳过原函数 (RegContext 底座)
// 静态: DobbyPatchInstrument — 把目标位置写死为计数/标记指令 (编译期式)
// ============================================================
#ifndef DOBBY_INSTRUMENT_H
#define DOBBY_INSTRUMENT_H
#include "dobby.h"
#include "regcontext.h"

#ifdef __cplusplus
extern "C" {
#endif

// 动态插桩 flags
#define DOBBY_INTERCEPT_DEFAULT            0
#define DOBBY_INTERCEPT_FP_SIMD            (1<<0)   // 含 NEON/VFP 上下文
#define DOBBY_INTERCEPT_CAN_SKIP_ORIGINAL  (1<<1)   // 允许回调里跳过原函数
#define DOBBY_INTERCEPT_RECORD             (1<<2)   // 记录拦截次数 (stats)

typedef struct DobbyInstrumentStats {
    uint64_t hit_count;      // 拦截命中次数
    uint64_t skip_count;     // 回调跳过原函数次数
} DobbyInstrumentStats;

// 动态插桩: 每次 target 被调时先执行 interceptor(env=RegContext, user)
// 返回值语义 (interceptor):
//   0 = 继续原函数; 非 0 = 已跳过原函数 (ICANSKIP 时)
void *DobbyIntercept(const char *lib, const char *symbol,
                     void (*interceptor)(DobbyRegContext *ctx, void *user),
                     void *user, uint32_t flags);
void *DobbyInterceptAddr(void *addr,
                         void (*interceptor)(DobbyRegContext *ctx, void *user),
                         void *user, uint32_t flags);
// 移除插桩
int DobbyUnintercept(void *stub);
// 统计读取 (RECORD 时)
bool DobbyInstrumentStatsGet(void *stub, DobbyInstrumentStats *out);

// 静态插桩: 把 addr 处 len 字节替换为自定义 patch (跨页安全), 原字节经 origin 拿到
int DobbyPatchInstrument(void *addr, const void *patch, size_t len, void *origin_backup);
int DobbyPatchRestore(void *addr, const void *original, size_t len);

} // extern "C"

#ifdef __cplusplus
namespace dobby {
class Instrument {
public:
  static void *Intercept(const char *lib, const char *symbol,
                         void (*interceptor)(DobbyRegContext *, void *),
                         void *user, uint32_t flags);
  static void *InterceptAddr(void *addr,
                             void (*interceptor)(DobbyRegContext *, void *),
                             void *user, uint32_t flags);
  static int Unintercept(void *stub);
  static bool StatsGet(void *stub, DobbyInstrumentStats *out);
  static int Patch(void *addr, const void *patch, size_t len, void *origin_backup);
  static int PatchRestore(void *addr, const void *original, size_t len);
};
}
#endif
#endif
