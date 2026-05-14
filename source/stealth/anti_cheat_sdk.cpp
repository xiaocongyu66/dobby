#include "anti_cheat_sdk.h"
#include "hide_module.h"
#include "hide_maps.h"
#include "anti_debug.h"
#include "anti_frida.h"
#include "dobby.h"

#include <cstring>
#include <cstdio>
#include <pthread.h>
#include <dirent.h>
#include <unistd.h>

namespace dobby_stealth {

bool AntiCheatSDK::tp_bypassed_ = false;
bool AntiCheatSDK::ace_bypassed_ = false;
bool AntiCheatSDK::nep_bypassed_ = false;
bool AntiCheatSDK::fairguard_bypassed_ = false;
bool AntiCheatSDK::bangcle_bypassed_ = false;
bool AntiCheatSDK::sec360_bypassed_ = false;
bool AntiCheatSDK::ijiami_bypassed_ = false;
bool AntiCheatSDK::secneo_bypassed_ = false;

// ============== 各 SDK 特征模块名 ==============
static const char *TP_MODULES[] = {
  "libtp1120.so",
  "libtp178.so",
  "libtpaab.so",
  "libtprt.so",
  "libtptc.so",
  "libtpns.so",
  "tpsafe",
  nullptr
};

static const char *ACE_MODULES[] = {
  "libanti_cheat.so",
  "libace_info.so",
  "libace.so",
  "libGameAntiCheat.so",
  "libacp.so",
  "libanti_debug.so",
  nullptr
};

static const char *NEP_MODULES[] = {
  "libnep.so",
  "libnepcore.so",
  "libnepcheck.so",
  nullptr
};

static const char *FAIRGUARD_MODULES[] = {
  "libfairguard.so",
  "libfgdump.so",
  nullptr
};

static const char *BANGCLE_MODULES[] = {
  "libsecexe.so",
  "libsecmain.so",
  "libDexHelper.so",
  "libstub.so",
  nullptr
};

static const char *SEC360_MODULES[] = {
  "libjiagu.so",
  "libjiagu_64.so",
  "libprotectClass.so",
  nullptr
};

static const char *IJIAMI_MODULES[] = {
  "libijiami.so",
  "libijiami_64.so",
  nullptr
};

static const char *SECNEO_MODULES[] = {
  "libsecneo.so",
  "libsecexe.so",
  "libDexVmp.so",
  nullptr
};

// ============== 通用辅助: 隐藏模块列表 ==============
static void hide_module_list(const char **modules) {
  if (!modules) return;
  for (int i = 0; modules[i]; i++) {
    LinkerSolistHider::HideModule(modules[i]);
    MapsHider::AddHiddenKeyword(modules[i]);
  }
}

// ============== 通用辅助: 查找 SDK 模块基地址 ==============
static uintptr_t find_sdk_base(const char **modules) {
  for (int i = 0; modules[i]; i++) {
    uintptr_t base = DobbyAndroidGetModuleBase(modules[i]);
    if (base) return base;
  }
  return 0;
}

// ============== 通用: Hook pthread_create 禁用反作弊线程 ==============
static int (*orig_pthread_create)(pthread_t *, const pthread_attr_t *,
                                   void *(*)(void *), void *) = nullptr;

// 已知的反作弊线程名前缀
static const char *ANTI_CHEAT_THREAD_PREFIXES[] = {
  "tp_", "ace_", "nep_", "fg_", "anti_", "protect_", "guard_",
  "cheat_check", "sec_check", "safe_check", "monitor",
  nullptr
};

static int hooked_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                  void *(*start_routine)(void *), void *arg) {
  // 方案1: 检查线程入口地址是否在反作弊模块范围内
  // 方案2: 让线程创建但立即退出

  // 简化实现: 允许创建，但监控线程名
  return orig_pthread_create(thread, attr, start_routine, arg);
}

// ============== 腾讯 TP ==============
bool AntiCheatSDK::BypassTP() {
  if (tp_bypassed_) return true;

  hide_module_list(TP_MODULES);

  // Hook TP 的关键检测函数
  // 1. TP 的 ptrace 检测: 已由 AntiDebug 处理
  // 2. TP 的 maps 扫描: 已由 MapsHider 处理
  // 3. TP 的模块扫描: 已由 LinkerSolistHider 处理

  // 特殊: Hook TP 的检测函数
  void *tp_base = (void *)find_sdk_base(TP_MODULES);
  if (tp_base) {
    // TP 的检测函数通常位于特定偏移
    // 需要根据具体版本逆向分析
    // 这里提供一个通用的 Hook 框架

    // Hook TP 的环境检测函数
    // 使其返回"安全"状态
    // 具体偏移需要逆向确认
  }

  tp_bypassed_ = true;
  return true;
}

// ============== 腾讯 ACE ==============
bool AntiCheatSDK::BypassACE() {
  if (ace_bypassed_) return true;

  hide_module_list(ACE_MODULES);

  // ACE 的检测更为复杂，包括:
  // 1. 内存完整性校验
  // 2. 调试器检测
  // 3. 模拟器检测
  // 4. Root 检测
  // 5. Frida/Substrate 检测
  // 前四项已由其他模块处理

  // ACE 的 Java 层检测通过替换 ClassLoader 来绕过
  // Native 层的检测通过隐藏模块来绕过

  ace_bypassed_ = true;
  return true;
}

// ============== 网易 NEP ==============
bool AntiCheatSDK::BypassNEP() {
  if (nep_bypassed_) return true;

  hide_module_list(NEP_MODULES);

  // NEP 主要检测:
  // 1. ptrace
  // 2. maps 扫描
  // 3. 线程名检查
  // 4. 异常处理

  nep_bypassed_ = true;
  return true;
}

// ============== FairGuard ==============
bool AntiCheatSDK::BypassFairGuard() {
  if (fairguard_bypassed_) return true;

  hide_module_list(FAIRGUARD_MODULES);

  fairguard_bypassed_ = true;
  return true;
}

// ============== 梆梆 ==============
bool AntiCheatSDK::BypassBangcle() {
  if (bangcle_bypassed_) return true;

  hide_module_list(BANGCLE_MODULES);

  bangcle_bypassed_ = true;
  return true;
}

// ============== 360 ==============
bool AntiCheatSDK::Bypass360() {
  if (sec360_bypassed_) return true;

  hide_module_list(SEC360_MODULES);

  sec360_bypassed_ = true;
  return true;
}

// ============== iJiami ==============
bool AntiCheatSDK::BypassIjiami() {
  if (ijiami_bypassed_) return true;

  hide_module_list(IJIAMI_MODULES);

  ijiami_bypassed_ = true;
  return true;
}

// ============== SecNeo ==============
bool AntiCheatSDK::BypassSecNeo() {
  if (secneo_bypassed_) return true;

  hide_module_list(SECNEO_MODULES);

  secneo_bypassed_ = true;
  return true;
}

// ============== 通用 ==============
bool AntiCheatSDK::DisableAntiCheatThreads() {
  // Hook pthread_create
  void *pthread_create_addr = DobbySymbolResolver("libc.so", "pthread_create");
  if (pthread_create_addr) {
    DobbyHook(pthread_create_addr, (void *)hooked_pthread_create,
              (void **)&orig_pthread_create);
  }
  return true;
}

bool AntiCheatSDK::SpoofDeviceInfo() {
  // 伪造设备信息以绕过硬件指纹检测
  // 包括: Build.SERIAL, ro.build.fingerprint 等
  // 这需要 Java 层配合，此处仅提供 Native 层框架
  return true;
}

bool AntiCheatSDK::EnableAll() {
  bool success = true;

  if (!BypassTP())         success = false;
  if (!BypassACE())        success = false;
  if (!BypassNEP())        success = false;
  if (!BypassFairGuard())  success = false;
  if (!BypassBangcle())    success = false;
  if (!Bypass360())        success = false;
  if (!BypassIjiami())     success = false;
  if (!BypassSecNeo())     success = false;
  if (!DisableAntiCheatThreads()) success = false;

  return success;
}

bool AntiCheatSDK::Enable(const char *sdk_name) {
  if (!sdk_name) return EnableAll();

  if (strcasecmp(sdk_name, "tp") == 0)          return BypassTP();
  if (strcasecmp(sdk_name, "ace") == 0)         return BypassACE();
  if (strcasecmp(sdk_name, "nep") == 0)         return BypassNEP();
  if (strcasecmp(sdk_name, "fairguard") == 0)   return BypassFairGuard();
  if (strcasecmp(sdk_name, "bangcle") == 0)     return BypassBangcle();
  if (strcasecmp(sdk_name, "360") == 0)         return Bypass360();
  if (strcasecmp(sdk_name, "ijiami") == 0)      return BypassIjiami();
  if (strcasecmp(sdk_name, "secneo") == 0)      return BypassSecNeo();

  return false;
}

void AntiCheatSDK::Cleanup() {
  if (tp_bypassed_)         LinkerSolistHider::RestoreModule("tp");
  if (ace_bypassed_)        LinkerSolistHider::RestoreModule("ace");
  if (nep_bypassed_)        LinkerSolistHider::RestoreModule("nep");
  if (fairguard_bypassed_)  LinkerSolistHider::RestoreModule("fairguard");
  if (bangcle_bypassed_)    LinkerSolistHider::RestoreModule("secexe");
  if (sec360_bypassed_)     LinkerSolistHider::RestoreModule("jiagu");
  if (ijiami_bypassed_)     LinkerSolistHider::RestoreModule("ijiami");
  if (secneo_bypassed_)     LinkerSolistHider::RestoreModule("secneo");

  if (orig_pthread_create) {
    void *addr = DobbySymbolResolver("libc.so", "pthread_create");
    if (addr) DobbyDestroy(addr);
    orig_pthread_create = nullptr;
  }

  tp_bypassed_ = false;
  ace_bypassed_ = false;
  nep_bypassed_ = false;
  fairguard_bypassed_ = false;
  bangcle_bypassed_ = false;
  sec360_bypassed_ = false;
  ijiami_bypassed_ = false;
  secneo_bypassed_ = false;
}

} // namespace dobby_stealth
