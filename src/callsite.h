#pragma once
#include "dobby.h"
namespace dobby {
class CallSite {
public:
  // site = BL/BLX 指令地址 (thumb: bit0=1). orig_callee_out 收原 callee.
  static int Hook(void *site, void **orig_callee_out, void *new_callee);
  static int Unhook(void *site);
  static void *OrigCallee(void *site);
};
}
