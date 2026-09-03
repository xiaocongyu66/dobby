#pragma once
#include "dobby.h"
namespace dobby {
class GuardSlot {
public:
  static int Add(void *slot, void *expected_value);   // 守卫 (expected=我们写的值)
  static int Remove(void *slot);
  static void Start();                                 // 启动 1s 守卫线程
};
}
