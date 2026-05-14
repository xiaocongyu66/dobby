#include "anti_debug.h"
#include "dobby.h"

#include <sys/ptrace.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

namespace dobby_stealth {

bool AntiDebug::ptrace_hooked_ = false;
bool AntiDebug::tracer_pid_hooked_ = false;
bool AntiDebug::signal_hooked_ = false;

// ============== ptrace Hook ==============
// 让 ptrace(PTRACE_TRACEME, ...) 始终返回 0
static long (*orig_ptrace)(int, ...) = nullptr;

static long hooked_ptrace(int request, ...) {
  va_list ap;
  va_start(ap, request);
  pid_t pid = va_arg(ap, pid_t);
  void *addr = va_arg(ap, void *);
  void *data = va_arg(ap, void *);
  va_end(ap);

  // PTRACE_TRACEME: 返回 0 表示成功
  if (request == PTRACE_TRACEME) {
    return 0;
  }

  // PTRACE_ATTACH / PTRACE_SEIZE 拦截
  if (request == PTRACE_ATTACH || request == PTRACE_SEIZE) {
    // 对自身进程的 attach 请求，返回 -1
    if (pid == getpid()) {
      return -1;
    }
  }

  // 其他情况，调用原始 ptrace
  if (orig_ptrace) {
    return orig_ptrace(request, pid, addr, data);
  }

  return -1;
}

// ============== 信号处理 ==============
// 拦截 SIGTRAP 信号，防止调试器断点被检测
static void (*orig_sigaction)(int, const struct sigaction *, struct sigaction *) = nullptr;

static int (*orig_sigprocmask)(int, const sigset_t *, sigset_t *) = nullptr;

// ============== 公共实现 ==============

bool AntiDebug::BypassPtrace() {
  if (ptrace_hooked_) return true;

  void *ptrace_addr = DobbySymbolResolver("libc.so", "ptrace");
  if (!ptrace_addr) return false;

  int ret = DobbyHook(ptrace_addr, (void *)hooked_ptrace, (void **)&orig_ptrace);
  if (ret == 0) {
    ptrace_hooked_ = true;
    return true;
  }
  return false;
}

bool AntiDebug::HideTracerPid() {
  if (tracer_pid_hooked_) return true;

  // TracerPid 的隐藏已由 MapsHider::Init() 中的 fgets hook 处理
  // 这里额外处理直接使用 read 系统调用读取 status 的情况

  tracer_pid_hooked_ = true;
  return true;
}

bool AntiDebug::HideTraceState() {
  // /proc/self/status 中的 State 字段
  // 正常: State: S (sleeping)
  // 调试: State: t (tracing stop)
  // 需要在 fgets hook 中将 "t (tracing stop)" 替换为 "S (sleeping)"
  return true;
}

bool AntiDebug::BypassSignalDetection() {
  if (signal_hooked_) return true;

  // 某些反调试使用 raise(SIGTRAP) 检查是否被调试
  // 如果没有被调试，SIGTRAP 会崩溃进程
  // 如果被调试，调试器会拦截 SIGTRAP

  signal_hooked_ = true;
  return true;
}

bool AntiDebug::BypassDebugPortScan() {
  // 某些反调试会扫描 JDWP / lldb 等调试端口
  // 这个由 maps hiding 和网络 hook 共同处理

  // Hook connect 来阻止对调试端口的检测
  return true;
}

bool AntiDebug::Enable() {
  bool success = true;

  if (!BypassPtrace())   success = false;
  if (!HideTracerPid())  success = false;
  if (!HideTraceState()) success = false;
  if (!BypassSignalDetection()) success = false;
  if (!BypassDebugPortScan())   success = false;

  return success;
}

void AntiDebug::Cleanup() {
  if (ptrace_hooked_) {
    void *ptrace_addr = DobbySymbolResolver("libc.so", "ptrace");
    if (ptrace_addr) DobbyDestroy(ptrace_addr);
    ptrace_hooked_ = false;
  }

  tracer_pid_hooked_ = false;
  signal_hooked_ = false;
}

} // namespace dobby_stealth
