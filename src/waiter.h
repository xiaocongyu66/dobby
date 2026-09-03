#pragma once
#include <stdint.h>
#include <stddef.h>

typedef void (*dobby_wait_cb_t)(int status, const char *lib, const char *symbol,
                                void *resolved, void *user);

namespace dobby {
class Waiter {
public:
  static void Start();
  static int Submit(const char *lib, const char *symbol, uintptr_t offset, bool use_offset,
                    void *replace, void **origin, uint32_t flags, uint32_t timeout_ms,
                    dobby_wait_cb_t cb, void *user);
  static int Cancel(const char *lib, const char *symbol);
  static int Pending();
  static int PendingCount();
};
}
