#pragma once

#include "InterceptRouting/RoutingPlugin.h"

class NearBranchTrampolinePlugin : public RoutingPluginInterface {};

extern bool g_enable_near_trampoline;

extern "C" PUBLIC void dobby_set_near_trampoline(bool enable);
