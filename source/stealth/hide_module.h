#pragma once

#include <dlfcn.h>
#include <link.h>
#include <string>
#include <vector>

namespace dobby_stealth {

// 从 linker 内部的 solist 中摘除指定模块
// 使 dl_iterate_phdr 和 /proc/self/maps 都不显示它
struct LinkerSolistHider {
  // 初始化，定位 linker 内部结构
  static bool Init();

  // 从 solist 中隐藏指定模块（按名称）
  static bool HideModule(const char *module_name);

  // 从 solist 中隐藏指定模块（按基地址）
  static bool HideModuleByBase(uintptr_t base);

  // 恢复隐藏的模块
  static bool RestoreModule(const char *module_name);

  // 清理所有隐藏
  static void Cleanup();

private:
  static bool initialized_;
  struct HiddenNode {
    std::string name;
    uintptr_t base;
    void *prev_node;  // soinfo*  (opaque)
    void *self_node;  // soinfo*  (opaque)
  };
  static std::vector<HiddenNode> hidden_nodes_;
};

} // namespace dobby_stealth
