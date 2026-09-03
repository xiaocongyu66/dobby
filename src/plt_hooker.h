#pragma once
namespace dobby {
class PltHooker {
public:
  static int Hook(const char *lib, const char *sym, void *replace, void **origin);
  static int Unhook(const char *lib, const char *sym);
  // xHook 同款正则批量 (实现在 plt_regex.cpp)
  static int HookRegex(const char *path_regex, const char *symbol,
                       void *replace, void **origin);
  static void Start();   // 启动 1s 补挂 worker
};
}
