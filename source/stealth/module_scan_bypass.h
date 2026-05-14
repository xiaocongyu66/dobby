#pragma once

#include <cstdint>

namespace dobby_stealth {

// 绕过模块扫描检测
// Hook dl_iterate_phdr 和相关函数，过滤隐藏的模块
struct ModuleScanBypass {
  // 启用模块扫描绕过
  static bool Enable();

  // 禁用
  static void Disable();

  // 添加需要从扫描结果中过滤的模块名
  static void AddFilteredModule(const char *module_name);

private:
  static bool enabled_;
};

} // namespace dobby_stealth
