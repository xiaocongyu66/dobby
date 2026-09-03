#pragma once
#include "dobby.h"
namespace dobby {
class InitArray {
public:
  // 读已加载 so 的 init_array 函数指针 (最多 max 个, 返回实际数)
  static int Read(const char *lib, uintptr_t *out, int max);
  // linker 级 init 拦截 (阶段 2: __dl_call_array)
  static int HookLinkerCall(void *pre_cb, void *user);
};
}
