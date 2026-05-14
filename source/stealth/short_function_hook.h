#pragma once

#include <cstdint>
#include <vector>

namespace dobby_stealth {

// 短函数 Hook 增强
// 当目标函数太短（指令不够覆盖跳转）时，Dobby 默认会失败
// 本模块提供替代方案:
// 1. 函数尾部 Hook: 跳到函数末尾再跳回来
// 2. 前置桩 Hook: 在函数调用者处拦截
// 3. PLT/GOT Hook: 直接替换 GOT 表项
// 4. Vtable Hook: 替换虚函数表指针
struct ShortFunctionHook {
  // 尝试用多种方法 Hook 短函数
  // 返回 0 成功，非零失败
  static int Hook(void *target, void *replace, void **origin);

  // PLT/GOT 表 Hook（最安全的方式）
  static int HookPLT(const char *image_name, const char *symbol,
                     void *replace, void **origin);

  // Vtable Hook（C++ 虚函数）
  static int HookVtable(void *object, int vtable_index,
                        void *replace, void **origin);

  // 函数调用者 Hook（在所有调用此函数的地方插入 Hook）
  static int HookCallers(const char *image_name, void *target,
                         void *replace, void **origin);

  // 判断函数是否可以被内联 Hook
  // 返回: >= 最小覆盖字节数, 0 = 太短
  static size_t GetMinPatchSize(void *target);
  static size_t GetActualPatchSize(void *target);

private:
  struct ShortHookEntry {
    void *target;
    void *replace;
    void *origin;
    int method; // 0=inline, 1=plt, 2=vtable, 3=caller
  };

  static std::vector<ShortHookEntry> entries_;
};

} // namespace dobby_stealth
