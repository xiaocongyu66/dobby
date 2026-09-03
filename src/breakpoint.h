#pragma once
#include "dobby.h"
#include "regcontext.h"
namespace dobby {
class Breakpoint {
public:
  // thumb 软件断点 (SIGTRAP): cb 收 RegContext (可改寄存器), 命中一次后失效
  static int Install(void *addr, void (*cb)(DobbyRegContext *, void *), void *user);
  static int ReArm(void *addr);    // 再次激活 (cb 循环观察用)
  static int Remove(void *addr);   // 移除并恢复原指令
};
}
