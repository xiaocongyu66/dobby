#pragma once

namespace dobby {
class RuntimeEventBus {
public:
  static RuntimeEventBus &Shared();
  void NotifyModuleLoaded(const char *name);
};
}
