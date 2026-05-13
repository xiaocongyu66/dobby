#pragma once

#include "core/arch/CpuRegister.h"

// Compatibility shim for older Dobby sources that still include core/arch/Cpu.h.
// Newer code keeps the register base type in CpuRegister.h, while a few arch
// specific files still need a small CpuFeatures declaration for cache flushing.
class CpuFeatures {
public:
  static void FlushICache(void *start, void *end);

  static void ClearCache(void *start, void *end) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin___clear_cache(reinterpret_cast<char *>(start), reinterpret_cast<char *>(end));
#else
    (void)start;
    (void)end;
#endif
  }
};
