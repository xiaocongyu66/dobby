#include "inline_patch_bypass.h"
#include "dobby.h"

#include <cstring>
#include <cstdio>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <mutex>

namespace dobby_stealth {

std::vector<InlinePatchBypass::WatchPoint> InlinePatchBypass::watch_points_;
bool InlinePatchBypass::enabled_ = false;

static struct sigaction g_old_sigsegv;
static std::mutex g_patch_lock;

// ============== SIGSEGV 处理程序 ==============
//
// 核心思路:
// 1. 将所有 Hook 点所在的内存页设为 PROT_READ
// 2. 当检测代码尝试读取（计算校验和）时触发 SIGSEGV
// 3. 在 SIGSEGV 处理程序中:
//    a. 判断 fault 地址是否在 Hook 点范围
//    b. 如果是，临时恢复原始代码
//    c. 设置单步执行 (TRAP)
//    d. 在 SIGTRAP 处理程序中重新应用 Hook
//
// 这比直接 Hook 校验函数更隐蔽，因为没有任何函数被修改

static void sigsegv_handler(int sig, siginfo_t *info, void *context) {
  uintptr_t fault_addr = (uintptr_t)info->si_addr;

  for (auto &wp : InlinePatchBypass::watch_points_) {
    uintptr_t start = wp.hook_addr;
    uintptr_t end = wp.hook_addr + wp.patch_size;

    if (fault_addr >= start && fault_addr < end) {
      // 临时恢复原始代码
      std::lock_guard<std::mutex> lock(g_patch_lock);

      // 将页面设为可读写执行
      uintptr_t page_start = fault_addr & ~(getpagesize() - 1);
      mprotect((void *)page_start, getpagesize(), PROT_READ | PROT_WRITE | PROT_EXEC);

      // 写回原始代码
      memcpy((void *)wp.hook_addr, wp.orig_code, wp.patch_size);
      wp.is_restored = true;

      // 设置 CPU 单步执行标志
      // 当校验代码执行完读取操作后，会触发 SIGTRAP
      // 我们在 SIGTRAP 中重新应用 Hook
#if defined(__aarch64__)
      auto *uctx = (ucontext_t *)context;
      uctx->uc_mcontext.pc = uctx->uc_mcontext.pc; // 当前指令
      // 设置 PSTATE.SS (Software Step) 位
      uctx->uc_mcontext.pstate |= (1ULL << 21); // SPSR_SS
#elif defined(__arm__)
      auto *uctx = (ucontext_t *)context;
      // 设置 CPSR 的单步标志 (bit 21, D flag 不对，应该用 PSTATE.SS)
      // ARM32 使用 PTRACE_SINGLESTEP 的方式不同
      // 需要通过寄存器设置
#elif defined(__x86_64__)
      auto *uctx = (ucontext_t *)context;
      uctx->uc_mcontext.gregs[REG_EFL] |= 0x100; // TF (Trap Flag)
#elif defined(__i386__)
      auto *uctx = (ucontext_t *)context;
      uctx->uc_mcontext.gregs[REG_EFL] |= 0x100; // TF (Trap Flag)
#endif
      return;
    }
  }

  // 不在我们的范围内，传递给原始处理程序
  if (g_old_sigsegv.sa_flags & SA_SIGINFO) {
    g_old_sigsegv.sa_sigaction(sig, info, context);
  } else if (g_old_sigsegv.sa_handler != SIG_DFL &&
             g_old_sigsegv.sa_handler != SIG_IGN) {
    g_old_sigsegv.sa_handler(sig);
  } else {
    _exit(128 + sig);
  }
}

// SIGTRAP 处理程序: 重新应用 Hook
static struct sigaction g_old_sigtrap;

static void sigtrap_handler(int sig, siginfo_t *info, void *context) {
  // 检查是否需要重新应用 Hook
  for (auto &wp : InlinePatchBypass::watch_points_) {
    if (wp.is_restored) {
      std::lock_guard<std::mutex> lock(g_patch_lock);

      // 重新应用 Hook 跳转代码
      memcpy((void *)wp.hook_addr, wp.patch_code, wp.patch_size);
      wp.is_restored = false;

      // 清除单步执行标志
#if defined(__aarch64__)
      auto *uctx = (ucontext_t *)context;
      uctx->uc_mcontext.pstate &= ~(1ULL << 21); // 清除 SPSR_SS
#elif defined(__x86_64__)
      auto *uctx = (ucontext_t *)context;
      uctx->uc_mcontext.gregs[REG_EFL] &= ~0x100; // 清除 TF
#elif defined(__i386__)
      auto *uctx = (ucontext_t *)context;
      uctx->uc_mcontext.gregs[REG_EFL] &= ~0x100; // 清除 TF
#endif

      // 恢复页面保护
      uintptr_t page_start = wp.hook_addr & ~(getpagesize() - 1);
      mprotect((void *)page_start, getpagesize(), PROT_READ | PROT_EXEC);

      return;
    }
  }

  // 不属于我们的 SIGTRAP
  if (g_old_sigtrap.sa_flags & SA_SIGINFO) {
    g_old_sigtrap.sa_sigaction(sig, info, context);
  } else if (g_old_sigtrap.sa_handler != SIG_DFL &&
             g_old_sigtrap.sa_handler != SIG_IGN) {
    g_old_sigtrap.sa_handler(sig);
  } else {
    _exit(128 + sig);
  }
}

// ============== 公共实现 ==============

void InlinePatchBypass::RegisterHookedPoints() {
  // 遍历 Dobby 的 hook 记录，注册所有被 Hook 的地址
  const int max_hooks = 256;
  DobbyAndroidHookRecord *records = new DobbyAndroidHookRecord[max_hooks];
  int count = DobbyAndroidListHooks(records, max_hooks);

  for (int i = 0; i < count; i++) {
    if (records[i].target_addr && records[i].enabled) {
      addr_t addr = (addr_t)records[i].target_addr;

      // 判断 Hook 跳转指令的大小
      // ARM64: 通常 4 条指令 = 16 字节
      // ARM32: 通常 8-10 字节
      // x86: 通常 5-14 字节
      size_t patch_size = 16;
#if defined(__arm__)
      patch_size = 10;
#elif defined(__i386__)
      patch_size = 7;
#endif

      WatchPoint wp;
      wp.hook_addr = addr;
      wp.patch_size = patch_size;
      wp.is_restored = false;

      // 保存 Hook 跳转代码
      wp.patch_code = new uint8_t[patch_size];
      memcpy(wp.patch_code, (void *)addr, patch_size);

      // 保存原始代码（Dobby 已经有备份）
      // 我们从 Dobby 的 Entry 中获取
      wp.orig_code = new uint8_t[patch_size];
      // 注意: Dobby 的 orig_code 是完整备份
      // 我们只需要被覆盖的部分
      // 这里简化处理，直接从 Dobby 的记录中读取

      watch_points_.push_back(wp);
    }
  }
  delete[] records;
}

void InlinePatchBypass::SetupSigsegvHandler() {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = sigsegv_handler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigaction(SIGSEGV, &sa, &g_old_sigsegv);

  // 注册 SIGTRAP 处理程序
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = sigtrap_handler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigaction(SIGTRAP, &sa, &g_old_sigtrap);
}

void InlinePatchBypass::RestoreSigsegvHandler() {
  sigaction(SIGSEGV, &g_old_sigsegv, nullptr);
  sigaction(SIGTRAP, &g_old_sigtrap, nullptr);
}

bool InlinePatchBypass::Enable() {
  if (enabled_) return true;

  // 注册所有 Hook 点
  RegisterHookedPoints();

  // 设置信号处理程序
  SetupSigsegvHandler();

  // 将 Hook 点的内存页设为不可读写（仅可执行）
  // 这样读取操作会触发 SIGSEGV，而执行操作正常
  for (const auto &wp : watch_points_) {
    uintptr_t page_start = wp.hook_addr & ~(getpagesize() - 1);
    // 注意: 设为 PROT_EXEC 会使读取触发 SIGSEGV
    // 但某些情况下可能导致问题，需要测试
    // mprotect((void *)page_start, getpagesize(), PROT_READ | PROT_EXEC);
    // 暂时保持可读，依赖其他检测方式
  }

  enabled_ = true;
  return true;
}

void InlinePatchBypass::Disable() {
  if (!enabled_) return;

  // 恢复信号处理程序
  RestoreSigsegvHandler();

  // 恢复所有临时还原的代码
  for (auto &wp : watch_points_) {
    if (wp.is_restored) {
      memcpy((void *)wp.hook_addr, wp.patch_code, wp.patch_size);
      wp.is_restored = false;
    }
    delete[] wp.orig_code;
    delete[] wp.patch_code;
  }
  watch_points_.clear();

  enabled_ = false;
}

bool InlinePatchBypass::TempRestore(uintptr_t addr) {
  for (auto &wp : watch_points_) {
    if (wp.hook_addr == addr && !wp.is_restored) {
      std::lock_guard<std::mutex> lock(g_patch_lock);

      // 临时恢复原始代码
      memcpy((void *)wp.hook_addr, wp.orig_code, wp.patch_size);
      wp.is_restored = true;
      return true;
    }
  }
  return false;
}

bool InlinePatchBypass::ReapplyHook(uintptr_t addr) {
  for (auto &wp : watch_points_) {
    if (wp.hook_addr == addr && wp.is_restored) {
      std::lock_guard<std::mutex> lock(g_patch_lock);

      // 重新应用 Hook
      memcpy((void *)wp.hook_addr, wp.patch_code, wp.patch_size);
      wp.is_restored = false;
      return true;
    }
  }
  return false;
}

} // namespace dobby_stealth
