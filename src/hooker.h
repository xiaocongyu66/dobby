#pragma once
#include "dobby.h"
namespace dobby {
class Hooker {
public:
  static int Hook(void *target, void *replace, void **origin);
  static int Unhook(void *target);
  static bool IsHooked(void *target);
  static int Count();
};
}
