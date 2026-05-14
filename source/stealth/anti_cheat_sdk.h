#pragma once

#include <cstdint>
#include <string>

namespace dobby_stealth {

// 绕过常见反作弊 SDK
struct AntiCheatSDK {
  // 启用所有已知反作弊 SDK 绕过
  static bool EnableAll();

  // 绕过指定 SDK
  // sdk_name: "tp", "ace", "nep", "fairguard", "secneo", "ijiami", "bangcle", "360", null=全部
  static bool Enable(const char *sdk_name);

  // 腾讯 TP (TencentProtect / tpsafe)
  static bool BypassTP();

  // 腾讯 ACE (Anti-Cheat Expert)
  static bool BypassACE();

  // 网易 NEP
  static bool BypassNEP();

  // FairGuard
  static bool BypassFairGuard();

  // 梆梆安全 (Bangcle)
  static bool BypassBangcle();

  // 360 加固
  static bool Bypass360();

  // iJiami (爱加密)
  static bool BypassIjiami();

  // SecNeo (梆梆企业版)
  static bool BypassSecNeo();

  // 通用: 禁用反作弊线程
  static bool DisableAntiCheatThreads();

  // 通用: 伪造设备信息
  static bool SpoofDeviceInfo();

  // 清理
  static void Cleanup();

private:
  static bool tp_bypassed_;
  static bool ace_bypassed_;
  static bool nep_bypassed_;
  static bool fairguard_bypassed_;
  static bool bangcle_bypassed_;
  static bool sec360_bypassed_;
  static bool ijiami_bypassed_;
  static bool secneo_bypassed_;
};

} // namespace dobby_stealth
