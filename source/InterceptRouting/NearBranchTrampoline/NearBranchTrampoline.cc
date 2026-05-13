#include "InterceptRouting/NearBranchTrampoline/NearBranchTrampoline.h"

bool g_enable_near_trampoline = false;

extern "C" PUBLIC void dobby_set_near_trampoline(bool enable) {
  g_enable_near_trampoline = enable;
}
