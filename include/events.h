#pragma once
#include "dobby.h"
namespace dobby {
void EmitEvent(void *target, void *replace, void *origin,
               const char *lib, const char *symbol, int status);
}
