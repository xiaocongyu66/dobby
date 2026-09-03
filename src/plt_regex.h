#pragma once
#include "dobby.h"
namespace dobby {
class PltRegex {
public:
  // xHook 同款: 正则匹配 so 路径 + 符号名, 批量替换 PLT 槽
  // 例: DobbyPltHookRegex(".*\\.so$", "exit", swallow_exit, nullptr)
  static int HookRegex(const char *path_regex, const char *symbol,
                       void *replace, void **origin);
  static int ApplyRules(uintptr_t base, const char *path);  // worker 调
};
}
