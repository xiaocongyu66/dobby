#pragma once

namespace dobby_stealth {

// 绕过 Substrate/Cydia 检测
struct AntiSubstrate {
  // 启用所有反 Substrate 绕过
  static bool Enable();

  // 隐藏 libsubstrate.so 模块
  static bool HideSubstrateModule();

  // 隐藏 Substrate 相关线程
  static bool HideSubstrateThreads();

  // 隐藏 Substrate 文件和映射
  static bool HideSubstrateFiles();

  // 清理
  static void Cleanup();

private:
  static bool module_hidden_;
  static bool threads_hidden_;
  static bool files_hidden_;
};

} // namespace dobby_stealth
