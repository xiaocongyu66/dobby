#pragma once
namespace dobby {
class LinkerMonitor {
public:
  static bool Start();
  static bool Subscribe(void (*on_load)(const char *path, void *user), void *user);
};
}
