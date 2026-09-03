#pragma once
#include "dobby.h"
#include <stddef.h>
namespace dobby {
class Asm {
public:
  // 文本汇编 → 机器码 (addr 用于 PC-rel 计算和 thumb 判定)
  static bool Assemble(const char *code, uintptr_t addr, unsigned char *out, size_t *out_size);
  // 直接汇编 patch 到目标地址
  static int PatchAsm(const char *code, void *addr);
};
}
