#pragma once
#include <stdint.h>

// 等待 hook 状态码 (公开 API 层复用)
#define DOBBY_WAIT_STATUS_PENDING     0
#define DOBBY_WAIT_STATUS_INSTALLED   1
#define DOBBY_WAIT_STATUS_TIMEOUT    -2
#define DOBBY_WAIT_STATUS_CANCELLED  -3
#define DOBBY_WAIT_STATUS_INVALID    -4

// flags
#define DOBBY_WAIT_FLAG_NONE       0

typedef void (*dobby_wait_callback_t)(int status, const char *lib, const char *symbol,
                                      void *resolved_addr, void *user_data);

namespace dobby {

class Waiter {
public:
  static int Submit(const char *lib, const char *symbol, uintptr_t offset, bool use_offset,
                    void *replace, void **origin, uint32_t flags, uint32_t timeout_ms,
                    dobby_wait_callback_t cb, void *user_data);
  static int Cancel(const char *lib, const char *symbol);
  static int PendingCount();
};

} // namespace dobby
