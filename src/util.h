#pragma once
#include <stdint.h>
#include <stddef.h>
namespace dobby {
class Util {
public:
  static int Protect(void *addr, size_t len, int prot);
  static int Write(void *addr, const void *buf, size_t len);
  static int Read(void *buf, const void *addr, size_t len);
  static void Nop(void *addr, size_t len);
  static void Flush(void *addr, size_t len);
};
}
