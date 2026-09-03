#pragma once
#include <stdint.h>
namespace dobby {
class Symbols {
public:
  static void *Find(const char *lib, const char *sym);
  static uintptr_t Base(const char *lib);
};
}
