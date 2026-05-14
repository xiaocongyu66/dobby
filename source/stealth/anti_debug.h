#pragma once

#include <cstdint>

namespace dobby_stealth {

// 绕过 ptrace / TracerPid / 调试标志 等反调试检测
struct AntiDebug {
  // 启用所有反调试绕过
  static bool Enable();

  // 仅绕过 ptrace 检测
  static bool BypassPtrace();

  // 仅隐藏 TracerPid
  static bool HideTracerPid();

  // 绕过 /proc/self/status 的 State 检测 (tracing stop)
  static bool HideTraceState();

  // 绕过信号检测 (SIGTRAP)
  static bool BypassSignalDetection();

  // 绕过调试器端口扫描
  static bool BypassDebugPortScan();

  // 清理
  static void Cleanup();

private:
  static bool ptrace_hooked_;
  static bool tracer_pid_hooked_;
  static bool signal_hooked_;
};

} // namespace dobby_stealth
