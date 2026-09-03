#pragma once
namespace dobby {
class PltHooker {
public:
  static int Hook(const char *lib, const char *sym, void *replace, void **origin);
  static int Unhook(const char *lib, const char *sym);
  static void Start();   // 启动 1s 补挂 worker
};
}
