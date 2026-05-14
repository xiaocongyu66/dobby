#include "dobby.h"

#if defined(__ANDROID__)

#include "hide_module.h"
#include "hide_maps.h"
#include "anti_debug.h"
#include "anti_frida.h"
#include "anti_substrate.h"
#include "integrity_bypass.h"
#include "inline_patch_bypass.h"
#include "module_scan_bypass.h"
#include "anti_cheat_sdk.h"

using namespace dobby_stealth;

// =====================================================================
// Dobby Stealth API Implementation
// =====================================================================

extern "C" {

int DobbyAndroidHideModule(const char *module_name) {
  if (!module_name) return -1;
  if (!LinkerSolistHider::Init()) return -1;
  if (!LinkerSolistHider::HideModule(module_name)) return -1;

  // 同时添加到 maps 和模块扫描的过滤列表
  MapsHider::AddHiddenKeyword(module_name);
  ModuleScanBypass::AddFilteredModule(module_name);

  return 0;
}

int DobbyAndroidRestoreModule(const char *module_name) {
  if (!module_name) return -1;
  if (!LinkerSolistHider::RestoreModule(module_name)) return -1;
  return 0;
}

int DobbyAndroidHideAllHookedModules() {
  if (!LinkerSolistHider::Init()) return -1;

  int hidden_count = 0;

  // 遍历所有已 Hook 的模块
  const int max_hooks = 256;
  DobbyAndroidHookRecord *records = new DobbyAndroidHookRecord[max_hooks];
  int count = DobbyAndroidListHooks(records, max_hooks);

  for (int i = 0; i < count; i++) {
    if (records[i].image_name[0] != '\0') {
      if (LinkerSolistHider::HideModule(records[i].image_name)) {
        MapsHider::AddHiddenKeyword(records[i].image_name);
        ModuleScanBypass::AddFilteredModule(records[i].image_name);
        hidden_count++;
      }
    }
  }

  delete[] records;
  return hidden_count;
}

int DobbyAndroidHideMapsInit() {
  if (!MapsHider::Init()) return -1;
  if (!ModuleScanBypass::Enable()) return -1;
  return 0;
}

void DobbyAndroidHideMapsKeyword(const char *keyword) {
  MapsHider::AddHiddenKeyword(keyword);
}

void DobbyAndroidHideMapsRange(uintptr_t start, uintptr_t end) {
  MapsHider::AddHiddenRange(start, end);
}

int DobbyAndroidHideMapsCleanup() {
  MapsHider::Cleanup();
  ModuleScanBypass::Disable();
  return 0;
}

int DobbyAndroidHideTracerPid() {
  if (!AntiDebug::HideTracerPid()) return -1;
  return 0;
}

int DobbyAndroidBypassAntiDebug() {
  if (!AntiDebug::Enable()) return -1;
  return 0;
}

int DobbyAndroidBypassAntiFrida() {
  if (!AntiFrida::Enable()) return -1;
  return 0;
}

int DobbyAndroidBypassAntiSubstrate() {
  if (!AntiSubstrate::Enable()) return -1;
  return 0;
}

int DobbyAndroidBypassIntegrityCheck(const char *image_name) {
  if (!IntegrityBypass::Enable(image_name)) return -1;
  return 0;
}

int DobbyAndroidBypassInlinePatchDetection() {
  if (!InlinePatchBypass::Enable()) return -1;
  return 0;
}

int DobbyAndroidBypassModuleScan() {
  if (!ModuleScanBypass::Enable()) return -1;
  return 0;
}

int DobbyAndroidBypassAntiCheatSDK(const char *sdk_name) {
  if (!sdk_name) {
    if (!AntiCheatSDK::EnableAll()) return -1;
  } else {
    if (!AntiCheatSDK::Enable(sdk_name)) return -1;
  }
  return 0;
}

int DobbyAndroidStealthMode() {
  int errors = 0;

  // 1. 初始化 linker solist 操作
  if (!LinkerSolistHider::Init()) errors++;

  // 2. 初始化 maps 和模块扫描隐藏
  if (DobbyAndroidHideMapsInit() != 0) errors++;

  // 3. 隐藏所有已 Hook 的模块
  DobbyAndroidHideAllHookedModules();

  // 4. 绕过反调试
  if (DobbyAndroidBypassAntiDebug() != 0) errors++;

  // 5. 绕过反 Frida
  if (DobbyAndroidBypassAntiFrida() != 0) errors++;

  // 6. 绕过反 Substrate
  if (DobbyAndroidBypassAntiSubstrate() != 0) errors++;

  // 7. 绕过完整性校验
  if (DobbyAndroidBypassIntegrityCheck(nullptr) != 0) errors++;

  // 8. 绕过 inline patch 检测
  if (DobbyAndroidBypassInlinePatchDetection() != 0) errors++;

  // 9. 绕过反作弊 SDK
  DobbyAndroidBypassAntiCheatSDK(nullptr);

  // 10. 隐藏 TracerPid
  DobbyAndroidHideTracerPid();

  return errors;
}

// =====================================================================
// Hook Capability Enhancements
// =====================================================================

int DobbyAndroidHookPLT(const char *image_name, const char *symbol,
                          void *replace, void **origin) {
  return ShortFunctionHook::HookPLT(image_name, symbol, replace, origin);
}

int DobbyAndroidHookVtable(void *object, int vtable_index,
                             void *replace, void **origin) {
  return ShortFunctionHook::HookVtable(object, vtable_index, replace, origin);
}

int DobbyAndroidHookShortFunction(void *target, void *replace, void **origin) {
  return ShortFunctionHook::Hook(target, replace, origin);
}

void DobbyAndroidEnableStealthTrampoline() {
  StealthTrampoline::Init();
  // After init, the stealth trampoline is ready
  // Future hooks will use randomized jump patterns
}

} // extern "C"

#endif // __ANDROID__
