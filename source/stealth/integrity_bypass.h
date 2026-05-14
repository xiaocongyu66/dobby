#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dobby_stealth {

// 绕过 .text 段哈希校验和完整性检查
struct IntegrityBypass {
  // 启用完整性校验绕过
  // image_name: 指定保护的 SO 名，NULL 表示保护所有已 Hook 的模块
  static bool Enable(const char *image_name);

  // 禁用
  static void Disable();

private:
  // 被保护的模块信息
  struct ProtectedModule {
    std::string name;
    uintptr_t base;
    size_t text_size;
    uint8_t *original_hash;  // 原始 .text 段的哈希
    size_t hash_size;
  };

  static std::vector<ProtectedModule> protected_modules_;
  static bool enabled_;

  // 计算 .text 段的哈希（简易版本，用于对比）
  static void compute_text_hash(uintptr_t base, size_t size,
                                 uint8_t *out_hash, size_t *out_size);

  // 查找 .text 段信息
  static bool find_text_section(uintptr_t base, uintptr_t *text_start, size_t *text_size);
};

} // namespace dobby_stealth
