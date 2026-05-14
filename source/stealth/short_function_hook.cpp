#include "short_function_hook.h"
#include "dobby.h"

#include <elf.h>
#include <link.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>
#include <cstdio>
#include <cstddef>

namespace dobby_stealth {

// Elf_Phdr 类型定义（根据架构选择 32/64 位）
#if defined(__aarch64__) || defined(__x86_64__)
using Elf_Phdr_Local = Elf64_Phdr;
#else
using Elf_Phdr_Local = Elf32_Phdr;
#endif

std::vector<ShortFunctionHook::ShortHookEntry> ShortFunctionHook::entries_;

// ============== 指令长度检测 ==============

#if defined(__aarch64__)
// ARM64: 每条指令固定 4 字节
// 内联 Hook 跳转需要至少 16 字节 (adrp + add + br = 3 条 = 12, 或 ldr + br = 16)
static const size_t MIN_PATCH_SIZE_ARM64 = 16;
#elif defined(__arm__)
// ARM32: ARM 模式 4 字节/条, Thumb 模式 2/4 字节/条
// Thumb LDR PC 跳转需要 8-10 字节
static const size_t MIN_PATCH_SIZE_ARM = 8;
static const size_t MIN_PATCH_SIZE_THUMB = 10;
#elif defined(__x86_64__)
// x86_64: 绝对跳转需要 14 字节 (movabs + jmp)
// 相对跳转需要 5 字节 (jmp rel32)
static const size_t MIN_PATCH_SIZE_X64 = 14;
#elif defined(__i386__)
// x86: 相对跳转 5 字节 (jmp rel32)
static const size_t MIN_PATCH_SIZE_X86 = 5;
#endif

size_t ShortFunctionHook::GetMinPatchSize(void *target) {
#if defined(__aarch64__)
  return MIN_PATCH_SIZE_ARM64;
#elif defined(__arm__)
  if ((uintptr_t)target & 1) // Thumb
    return MIN_PATCH_SIZE_THUMB;
  else
    return MIN_PATCH_SIZE_ARM;
#elif defined(__x86_64__)
  return MIN_PATCH_SIZE_X64;
#elif defined(__i386__)
  return MIN_PATCH_SIZE_X86;
#else
  return 16;
#endif
}

size_t ShortFunctionHook::GetActualPatchSize(void *target) {
  // 分析目标函数前几条指令
  // 返回从函数开始到可以安全覆盖的边界
  uintptr_t addr = (uintptr_t)target;

#if defined(__aarch64__)
  // ARM64: 每条指令 4 字节，最小需要 4 条
  // 检查前几条指令是否有 PC-Relative 或分支
  // 简化: 返回最小覆盖长度
  return MIN_PATCH_SIZE_ARM64;
#elif defined(__arm__)
  if (addr & 1) {
    // Thumb: 需要检查指令长度 (2 或 4 字节)
    uint16_t *insns = (uint16_t *)(addr & ~1);
    size_t total = 0;
    while (total < MIN_PATCH_SIZE_THUMB) {
      // 32-bit Thumb 指令判断
      if ((insns[total / 2] & 0xE000) == 0xE000 &&
          (insns[total / 2] & 0x1800) != 0x0000) {
        total += 4; // 32-bit Thumb
      } else {
        total += 2; // 16-bit Thumb
      }
    }
    return total;
  } else {
    return MIN_PATCH_SIZE_ARM;
  }
#else
  return GetMinPatchSize(target);
#endif
}

// ============== PLT/GOT Hook ==============

int ShortFunctionHook::HookPLT(const char *image_name, const char *symbol,
                                void *replace, void **origin) {
  if (!symbol || !replace) return -1;

  // 方法1: 使用 Dobby 自带的 ImportTableReplace
  int ret = DobbyImportTableReplace((char *)image_name, (char *)symbol,
                                     replace, origin);
  if (ret == 0) {
    ShortHookEntry entry;
    entry.target = nullptr;
    entry.replace = replace;
    entry.origin = origin ? *origin : nullptr;
    entry.method = 1; // PLT
    entries_.push_back(entry);
    return 0;
  }

  // 方法2: 手动修改 GOT 表项
  // 1. 找到目标模块的 GOT 表
  // 2. 找到符号对应的 GOT 表项
  // 3. 替换其中的地址

  uintptr_t module_base = DobbyAndroidGetModuleBase(image_name);
  if (!module_base) return -1;

  // 解析 ELF，找到 .rel.plt / .rela.plt 中的符号
  // 然后修改对应的 GOT 表项

#if defined(__aarch64__) || defined(__x86_64__)
  using Elf_Ehdr = Elf64_Ehdr;
  using Elf_Shdr = Elf64_Shdr;
  using Elf_Rela = Elf64_Rela;
  using Elf_Sym = Elf64_Sym;
  using Elf_Dyn = Elf64_Dyn;
#else
  using Elf_Ehdr = Elf32_Ehdr;
  using Elf_Shdr = Elf32_Shdr;
  using Elf_Rel = Elf32_Rel;
  using Elf_Sym = Elf32_Sym;
  using Elf_Dyn = Elf32_Dyn;
#endif

  auto *ehdr = (Elf_Ehdr *)module_base;
  if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) return -1;

  // 遍历动态段查找 JMPREL 和 PLTGOT
  auto *phdr = (Elf_Phdr_Local *)(module_base + ehdr->e_phoff);
  uintptr_t dyn_addr = 0;
  size_t dyn_size = 0;

  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_DYNAMIC) {
      dyn_addr = module_base + phdr[i].p_vaddr;
      dyn_size = phdr[i].p_filesz;
      break;
    }
  }

  if (!dyn_addr) return -1;

  // 解析动态段，找到相关表
  uintptr_t jmprel_addr = 0, symtab_addr = 0, strtab_addr = 0;
  uintptr_t pltgot_addr = 0;
  size_t jmprel_size = 0;
  size_t symtab_entsize = sizeof(Elf_Sym);

#if defined(__aarch64__) || defined(__x86_64__)
  auto *dyn = (Elf_Dyn *)dyn_addr;
  for (size_t i = 0; i < dyn_size / sizeof(Elf_Dyn); i++) {
    switch (dyn[i].d_tag) {
      case DT_JMPREL:     jmprel_addr = dyn[i].d_un.d_ptr; break;
      case DT_PLTRELSZ:   jmprel_size = dyn[i].d_un.d_val; break;
      case DT_SYMTAB:     symtab_addr = dyn[i].d_un.d_ptr; break;
      case DT_STRTAB:     strtab_addr = dyn[i].d_un.d_ptr; break;
      case DT_PLTGOT:     pltgot_addr = dyn[i].d_un.d_ptr; break;
      case DT_SYMENT:     symtab_entsize = dyn[i].d_un.d_val; break;
    }
  }
#endif

  if (!jmprel_addr || !symtab_addr || !strtab_addr) return -1;

  // 遍历重定位表，查找目标符号
#if defined(__aarch64__) || defined(__x86_64__)
  auto *rela = (Elf64_Rela *)jmprel_addr;
  size_t rela_count = jmprel_size / sizeof(Elf64_Rela);

  for (size_t i = 0; i < rela_count; i++) {
    size_t sym_idx = ELF64_R_SYM(rela[i].r_info);
    auto *sym = (Elf_Sym *)(symtab_addr + sym_idx * symtab_entsize);
    const char *name = (const char *)(strtab_addr + sym->st_name);

    if (strcmp(name, symbol) == 0) {
      // 找到目标符号，修改 GOT 表项
      void **got_entry = (void **)(module_base + rela[i].r_offset);

      // 保存原始值
      if (origin) *origin = *got_entry;

      // 修改页保护并写入
      uintptr_t page = (uintptr_t)got_entry & ~(getpagesize() - 1);
      mprotect((void *)page, getpagesize(), PROT_READ | PROT_WRITE);
      *got_entry = replace;

      ShortHookEntry entry;
      entry.target = got_entry;
      entry.replace = replace;
      entry.origin = origin ? *origin : nullptr;
      entry.method = 1;
      entries_.push_back(entry);

      return 0;
    }
  }
#endif

  return -1;
}

// ============== Vtable Hook ==============

int ShortFunctionHook::HookVtable(void *object, int vtable_index,
                                   void *replace, void **origin) {
  if (!object || vtable_index < 0) return -1;

  // C++ 对象的内存布局: 第一个字段是 vtable 指针
  void ***vtable_ptr = (void ***)object;
  void **vtable = *vtable_ptr;

  if (!vtable) return -1;

  // 保存原始虚函数指针
  if (origin) *origin = vtable[vtable_index];

  // 替换虚函数指针
  // 注意: vtable 通常在只读内存中，需要修改页保护
  uintptr_t page = (uintptr_t)&vtable[vtable_index] & ~(getpagesize() - 1);
  mprotect((void *)page, getpagesize(), PROT_READ | PROT_WRITE);
  vtable[vtable_index] = replace;

  ShortHookEntry entry;
  entry.target = &vtable[vtable_index];
  entry.replace = replace;
  entry.origin = origin ? *origin : nullptr;
  entry.method = 2; // Vtable
  entries_.push_back(entry);

  return 0;
}

// ============== 函数调用者 Hook ==============

int ShortFunctionHook::HookCallers(const char *image_name, void *target,
                                    void *replace, void **origin) {
  // 在指定模块中搜索所有调用目标函数的 call/bl 指令
  // 将这些指令的调用目标替换为我们的 Hook 函数

  uintptr_t module_base = DobbyAndroidGetModuleBase(image_name);
  if (!module_base) return -1;

  uintptr_t target_addr = (uintptr_t)target;
  int patched_count = 0;

  // 扫描代码段
  // 查找 BL/BLR (ARM) 或 CALL (x86) 指令
  // 这需要指令解码器，简化实现:

  // 方法: 利用 dl_iterate_phdr 找到代码段范围
  // 然后逐条扫描 BL/CALL 指令

  return patched_count > 0 ? 0 : -1;
}

// ============== 智能短函数 Hook ==============

int ShortFunctionHook::Hook(void *target, void *replace, void **origin) {
  if (!target || !replace) return -1;

  // 先尝试标准内联 Hook
  int ret = DobbyHook(target, replace, origin);
  if (ret == 0) return 0;

  // 如果内联 Hook 失败（可能因为函数太短），尝试替代方案

  // 尝试 PLT Hook (如果目标是导入函数)
  // 尝试 Near Branch Hook
  // 尝试函数调用者 Hook

  // 短函数的替代方案: 前置代码池
  // 在目标函数附近分配一块代码，作为跳板
  // 将目标函数的第一条指令修改为跳转到跳板
  // 在跳板中执行原始指令并跳转到 Hook 函数

  ShortHookEntry entry;
  entry.target = target;
  entry.replace = replace;
  entry.origin = origin ? *origin : nullptr;
  entry.method = 0;
  entries_.push_back(entry);

  return -1; // 所有方案都失败
}

} // namespace dobby_stealth
