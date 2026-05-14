#include "anti_substrate.h"
#include "hide_module.h"
#include "hide_maps.h"
#include "dobby.h"

#include <cstring>
#include <cstdio>

namespace dobby_stealth {

bool AntiSubstrate::module_hidden_ = false;
bool AntiSubstrate::threads_hidden_ = false;
bool AntiSubstrate::files_hidden_ = false;

// ============== Substrate 特征关键词 ==============
static const char *SUBSTRATE_KEYWORDS[] = {
  "substrate",
  "libsubstrate.so",
  "substrate-dvm",
  "CydiaSubstrate",
  "MSHookFunction",
  "MSFindSymbol",
  "MSGetImageByName",
  "SubstrateHook",
  "SubstratePosixMemory",
  "com.saurik.substrate",
  "com.cydia.substrate",
  nullptr
};

bool AntiSubstrate::HideSubstrateModule() {
  if (module_hidden_) return true;

  // 从 linker solist 中摘除 substrate 模块
  LinkerSolistHider::HideModule("substrate");
  LinkerSolistHider::HideModule("CydiaSubstrate");

  module_hidden_ = true;
  return true;
}

bool AntiSubstrate::HideSubstrateThreads() {
  if (threads_hidden_) return true;

  // Substrate 一般不以独立线程运行，但某些包装器可能创建
  // 这里主要防止线程名包含 substrate 关键字
  threads_hidden_ = true;
  return true;
}

bool AntiSubstrate::HideSubstrateFiles() {
  if (files_hidden_) return true;

  // 将 Substrate 关键词添加到 maps 隐藏
  for (int i = 0; SUBSTRATE_KEYWORDS[i]; i++) {
    MapsHider::AddHiddenKeyword(SUBSTRATE_KEYWORDS[i]);
  }

  files_hidden_ = true;
  return true;
}

bool AntiSubstrate::Enable() {
  bool success = true;

  if (!HideSubstrateModule())  success = false;
  if (!HideSubstrateThreads()) success = false;
  if (!HideSubstrateFiles())   success = false;

  return success;
}

void AntiSubstrate::Cleanup() {
  if (module_hidden_) {
    LinkerSolistHider::RestoreModule("substrate");
    LinkerSolistHider::RestoreModule("CydiaSubstrate");
    module_hidden_ = false;
  }
  threads_hidden_ = false;
  files_hidden_ = false;
}

} // namespace dobby_stealth
