#pragma once
#include <stdint.h>
#include <stddef.h>

namespace dobby {

class Stealth {
public:
  // trampoline 匿名页伪装 (PR_SET_VMA 改名为系统段名)
  static bool DisguisePage(void *page_addr, size_t size);
  // 跳转指令编码随机化 (防 CRC 快照特征)
  static uint32_t RandomizeJump(uint32_t preferred, void *from, void *to);
  // hook 完整性: 安装时快照 / 定期校验 (integrity violation 检测)
  static void SnapshotIntegrity(void *addr, uint32_t installed_head);
  static bool VerifyIntegrity(void *addr);
};

} // namespace dobby
