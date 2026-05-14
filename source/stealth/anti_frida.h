#pragma once

#include <cstdint>

namespace dobby_stealth {

// 绕过各种 Frida 检测手段
struct AntiFrida {
  // 启用所有反 Frida 绕过
  static bool Enable();

  // 隐藏 Frida 线程名 (frida-gum-js-loop 等)
  static bool HideFridaThreads();

  // 隐藏 Frida 监听端口 (27042)
  static bool HideFridaPorts();

  // 隐藏 Frida 相关文件和目录
  static bool HideFridaFiles();

  // 隐藏 Frida 特征字符串 (DroidPage, LIBFRIDA 等)
  static bool HideFridaStrings();

  // 隐藏 Frida 内存映射
  static bool HideFridaMemory();

  // 修补 Frida 特征端口
  static bool PatchFridaPort();

  // 清理
  static void Cleanup();

private:
  static bool threads_hidden_;
  static bool ports_hidden_;
  static bool files_hidden_;
  static bool strings_hidden_;
  static bool memory_hidden_;
};

} // namespace dobby_stealth
