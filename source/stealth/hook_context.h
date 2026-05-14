#pragma once

#include <cstdint>
#include <string>

#if defined(__aarch64__) || defined(__arm64__) || defined(__arm__)
#include "dobby.h" // DobbyRegisterContext
#endif

namespace dobby_stealth {

// Hook 上下文增强
// 提供对寄存器和堆栈的高级操作，简化 Hook 函数的编写
struct HookContext {
#if defined(__aarch64__) || defined(__arm64__)
  DobbyRegisterContext *ctx;

  // 读写通用寄存器
  uint64_t GetX(int n) const;
  void SetX(int n, uint64_t val);

  // 便捷方法
  uint64_t GetArg0() const { return GetX(0); }
  uint64_t GetArg1() const { return GetX(1); }
  uint64_t GetArg2() const { return GetX(2); }
  uint64_t GetArg3() const { return GetX(3); }
  uint64_t GetArg4() const { return GetX(4); }
  uint64_t GetArg5() const { return GetX(5); }
  uint64_t GetArg6() const { return GetX(6); }
  uint64_t GetArg7() const { return GetX(7); }

  void SetArg0(uint64_t v) { SetX(0, v); }
  void SetArg1(uint64_t v) { SetX(1, v); }
  void SetArg2(uint64_t v) { SetX(2, v); }
  void SetArg3(uint64_t v) { SetX(3, v); }
  void SetArg4(uint64_t v) { SetX(4, v); }
  void SetArg5(uint64_t v) { SetX(5, v); }
  void SetArg6(uint64_t v) { SetX(6, v); }
  void SetArg7(uint64_t v) { SetX(7, v); }

  uint64_t GetSP() const;
  void SetSP(uint64_t val);
  uint64_t GetLR() const;
  void SetLR(uint64_t val);
  uint64_t GetFP() const;
  void SetFP(uint64_t val);

  // 读写堆栈
  uint64_t ReadStack(int offset) const;
  void WriteStack(int offset, uint64_t val);

  // 读取指针指向的内存
  uint64_t ReadMemory(uint64_t addr) const;
  void WriteMemory(uint64_t addr, uint64_t val);

  // 读取字符串
  std::string ReadCString(uint64_t addr) const;

#elif defined(__arm__)
  DobbyRegisterContext *ctx;

  uint32_t GetR(int n) const;
  void SetR(int n, uint32_t val);

  uint32_t GetArg0() const { return GetR(0); }
  uint32_t GetArg1() const { return GetR(1); }
  uint32_t GetArg2() const { return GetR(2); }
  uint32_t GetArg3() const { return GetR(3); }

  void SetArg0(uint32_t v) { SetR(0, v); }
  void SetArg1(uint32_t v) { SetR(1, v); }
  void SetArg2(uint32_t v) { SetR(2, v); }
  void SetArg3(uint32_t v) { SetR(3, v); }

  uint32_t GetSP() const;
  void SetSP(uint32_t val);
  uint32_t GetLR() const;
  void SetLR(uint32_t val);
#endif

#if defined(__aarch64__) || defined(__arm64__) || defined(__arm__)
  // 构造
  HookContext(DobbyRegisterContext *c) : ctx(c) {}
#else
  // 构造（其他架构）
  HookContext(void *c) : ctx(c) {}
#endif
};

} // namespace dobby_stealth
