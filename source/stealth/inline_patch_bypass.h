#pragma once

#include <cstdint>
#include <vector>

namespace dobby_stealth {

// 绕过 inline hook 检测
// 检测方法: 检查函数头是否被修改为跳转指令
struct InlinePatchBypass {
  // 启用 inline patch 绕过
  static bool Enable();

  // 禁用
  static void Disable();

  // 临时恢复指定地址的原始代码（用于校验期间）
  static bool TempRestore(addr_t addr);

  // 重新应用 Hook
  static bool ReapplyHook(addr_t addr);

private:
  struct WatchPoint {
    addr_t hook_addr;      // Hook 点地址
    size_t patch_size;     // 被修改的代码大小
    uint8_t *orig_code;    // 原始代码备份
    uint8_t *patch_code;   // Hook 跳转代码
    bool is_restored;      // 是否已临时恢复
  };

  static std::vector<WatchPoint> watch_points_;
  static bool enabled_;

  // 注册所有已 Hook 的点
  static void RegisterHookedPoints();

  // SIGSEGV 处理
  static void SetupSigsegvHandler();
  static void RestoreSigsegvHandler();
};

} // namespace dobby_stealth
