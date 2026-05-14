#include "integrity_bypass.h"
#include "dobby.h"

#include <elf.h>
#include <link.h>
#include <cstring>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <mutex>

namespace dobby_stealth {

std::vector<IntegrityBypass::ProtectedModule> IntegrityBypass::protected_modules_;
bool IntegrityBypass::enabled_ = false;

// ============== ELF 解析 ==============

#if defined(__aarch64__) || defined(__x86_64__)
using Elf_Ehdr = Elf64_Ehdr;
using Elf_Shdr = Elf64_Shdr;
using Elf_Phdr = Elf64_Phdr;
#else
using Elf_Ehdr = Elf32_Ehdr;
using Elf_Shdr = Elf32_Shdr;
using Elf_Phdr = Elf32_Phdr;
#endif

bool IntegrityBypass::find_text_section(uintptr_t base, uintptr_t *text_start, size_t *text_size) {
  if (!base || !text_start || !text_size) return false;

  auto *ehdr = (Elf_Ehdr *)base;
  if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) return false;

  // 通过 Program Header 查找可执行段
  auto *phdr = (Elf_Phdr *)(base + ehdr->e_phoff);
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD && (phdr[i].p_flags & PF_X)) {
      *text_start = base + phdr[i].p_vaddr;
      *text_size = phdr[i].p_filesz;
      return true;
    }
  }

  // 回退: 通过 Section Header 查找 .text
  if (ehdr->e_shoff && ehdr->e_shnum) {
    auto *shdr = (Elf_Shdr *)(base + ehdr->e_shoff);
    const char *shstrtab = (const char *)(base + shdr[ehdr->e_shstrndx].sh_offset);

    for (int i = 0; i < ehdr->e_shnum; i++) {
      if (strcmp(shstrtab + shdr[i].sh_name, ".text") == 0) {
        *text_start = base + shdr[i].sh_addr;
        *text_size = shdr[i].sh_size;
        return true;
      }
    }
  }

  return false;
}

void IntegrityBypass::compute_text_hash(uintptr_t base, size_t size,
                                         uint8_t *out_hash, size_t *out_size) {
  // 简易哈希: 32-byte XOR hash
  // 生产环境应替换为 SHA256 或 CRC32
  size_t hash_size = 32;
  memset(out_hash, 0, hash_size);

  uint8_t *data = (uint8_t *)base;
  for (size_t i = 0; i < size; i++) {
    out_hash[i % hash_size] ^= data[i];
  }

  *out_size = hash_size;
}

// ============== SIGSEGV 处理程序 ==============
// 方法1: mprotect + SIGSEGV 信号处理
// 将被 Hook 的内存页面设为只读/不可读
// 当检测代码尝试读取（计算校验和）时触发 SIGSEGV
// 在信号处理程序中临时恢复原始代码

static struct sigaction g_old_sigsegv;
static std::mutex g_integrity_lock;

// 记录正在被校验的线程
static pid_t g_checking_tid = 0;

static void sigsegv_handler(int sig, siginfo_t *info, void *context) {
  uintptr_t fault_addr = (uintptr_t)info->si_addr;

  // 检查是否在我们的保护范围内
  for (const auto &mod : protected_modules_) {
    uintptr_t text_start = mod.base;
    uintptr_t text_end = mod.base + mod.text_size;

    if (fault_addr >= text_start && fault_addr < text_end) {
      // 临时恢复原始代码
      // 使用 Dobby 的 backup_orig_code 机制
      // 这里需要临时 mprotect 为可写，恢复原始字节，执行校验，再重新 Hook

      // 注意: 这只是简化实现
      // 完整实现需要:
      // 1. 暂停当前线程
      // 2. 恢复原始代码
      // 3. 设置单步执行
      // 4. 在校验完成后重新应用 Hook

      // 临时设置页面为可读写执行
      uintptr_t page_start = fault_addr & ~(getpagesize() - 1);
      mprotect((void *)page_start, getpagesize(), PROT_READ | PROT_WRITE | PROT_EXEC);

      // 让检测代码读取到"正确"的内容
      // 这里我们简单地将页面设为可读，让检测代码继续
      // 更高级的实现会在读取后重新设为不可读

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

// 方法2: Hook 校验函数本身
// 更简单且更可靠: Hook 计算 hash 的函数，让它返回预期的值
// 这需要知道目标 SDK 使用的具体校验函数

// ============== 公共实现 ==============

bool IntegrityBypass::Enable(const char *image_name) {
  if (enabled_) return true;

  // 注册 SIGSEGV 处理程序
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = sigsegv_handler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigaction(SIGSEGV, &sa, &g_old_sigsegv);

  if (image_name) {
    // 保护指定模块
    uintptr_t base = 0;
    dl_iterate_phdr([](struct dl_phdr_info *info, size_t size, void *data) -> int {
      auto *target_base = (uintptr_t *)data;
      // 在这里 info->dlpi_name 包含模块名
      // 匹配 image_name
      if (info->dlpi_name && strstr(info->dlpi_name, (const char *)data)) {
        *target_base = info->dlpi_addr;
        return 1;
      }
      return 0;
    }, &base);

    if (base) {
      uintptr_t text_start;
      size_t text_size;
      if (find_text_section(base, &text_start, &text_size)) {
        ProtectedModule mod;
        mod.name = image_name;
        mod.base = text_start;
        mod.text_size = text_size;
        mod.hash_size = 32;
        mod.original_hash = new uint8_t[mod.hash_size];
        compute_text_hash(text_start, text_size, mod.original_hash, &mod.hash_size);
        protected_modules_.push_back(mod);
      }
    }
  } else {
    // 保护所有已 Hook 的模块
    const int max_hooks = 256;
    DobbyAndroidHookRecord *records = new DobbyAndroidHookRecord[max_hooks];
    int count = DobbyAndroidListHooks(records, max_hooks);

    for (int i = 0; i < count; i++) {
      if (records[i].image_name[0] != '\0') {
        uintptr_t base = DobbyAndroidGetModuleBase(records[i].image_name);
        if (base) {
          uintptr_t text_start;
          size_t text_size;
          if (find_text_section(base, &text_start, &text_size)) {
            ProtectedModule mod;
            mod.name = records[i].image_name;
            mod.base = text_start;
            mod.text_size = text_size;
            mod.hash_size = 32;
            mod.original_hash = new uint8_t[mod.hash_size];
            compute_text_hash(text_start, text_size, mod.original_hash, &mod.hash_size);
            protected_modules_.push_back(mod);
          }
        }
      }
    }
    delete[] records;
  }

  enabled_ = true;
  return true;
}

void IntegrityBypass::Disable() {
  if (!enabled_) return;

  // 恢复原始 SIGSEGV 处理程序
  sigaction(SIGSEGV, &g_old_sigsegv, nullptr);

  // 清理
  for (auto &mod : protected_modules_) {
    delete[] mod.original_hash;
  }
  protected_modules_.clear();
  enabled_ = false;
}

} // namespace dobby_stealth
