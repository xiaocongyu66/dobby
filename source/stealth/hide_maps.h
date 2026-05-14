#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace dobby_stealth {

// Hook 底层文件操作，过滤 /proc/self/maps 中的敏感行
// 同时过滤 /proc/self/status 中的 TracerPid
struct MapsHider {
  // 初始化，安装 Hook
  static bool Init();

  // 添加需要隐藏的模块名关键词
  static void AddHiddenKeyword(const char *keyword);

  // 添加需要隐藏的内存地址范围
  static void AddHiddenRange(uintptr_t start, uintptr_t end);

  // 自动隐藏所有已 Hook 的模块占用的内存区域
  static void AutoHideHookedModules();

  // 清理所有 Hook
  static void Cleanup();

private:
  static std::vector<std::string> hidden_keywords_;
  static std::vector<std::pair<uintptr_t, uintptr_t>> hidden_ranges_;
  static bool initialized_;
};

} // namespace dobby_stealth
