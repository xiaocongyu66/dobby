#include "stealth_trampoline.h"

#include <cstring>
#include <cstdlib>
#include <ctime>
#include <sys/mman.h>

namespace dobby_stealth {

bool StealthTrampoline::initialized_ = false;
uint32_t StealthTrampoline::random_seed_ = 0;

// ARM64 可用的临时寄存器（避开 x0-x7 参数寄存器，x16/x17 IP 寄存器，x29/x30 fp/lr）
const int StealthTrampoline::ARM64_TEMP_REGS[] = {
  8, 9, 10, 11, 12, 13, 14, 15, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28
};
const int StealthTrampoline::ARM64_TEMP_REG_COUNT =
    sizeof(ARM64_TEMP_REGS) / sizeof(ARM64_TEMP_REGS[0]);

void StealthTrampoline::Init() {
  if (initialized_) return;
  random_seed_ = (uint32_t)time(nullptr) ^ (uint32_t)getpid();
  srand(random_seed_);
  initialized_ = true;
}

int StealthTrampoline::RandomTempReg() {
  if (!initialized_) Init();
  return ARM64_TEMP_REGS[rand() % ARM64_TEMP_REG_COUNT];
}

// ============== ARM64 指令编码辅助 ==============

static inline uint32_t arm64_movz(int rd, uint16_t imm, int shift) {
  // MOVZ Rd, #imm{, LSL #shift}
  int hw = shift / 16;
  return (0xD2800000 | (hw << 21) | ((uint32_t)imm << 5) | rd);
}

static inline uint32_t arm64_movk(int rd, uint16_t imm, int shift) {
  // MOVK Rd, #imm{, LSL #shift}
  int hw = shift / 16;
  return (0xF2800000 | (hw << 21) | ((uint32_t)imm << 5) | rd);
}

static inline uint32_t arm64_br(int rn) {
  // BR Xn
  return (0xD61F0000 | (rn << 5));
}

static inline uint32_t arm64_ldr_pc_offset(int rt, int32_t offset) {
  // LDR Xt, [PC, #offset]  (offset must be aligned and positive multiple of 4)
  uint32_t imm19 = ((uint32_t)offset >> 2) & 0x7FFFF;
  return (0x58000000 | (imm19 << 5) | rt);
}

static inline uint32_t arm64_adrp(int rd, int64_t offset) {
  // ADRP Rd, label
  uint64_t pc_page_bits = 0; // caller fills in
  int64_t page_offset = offset >> 12;
  uint32_t immlo = (uint32_t)(page_offset & 0x3);
  uint32_t immhi = (uint32_t)((page_offset >> 2) & 0x7FFFF);
  return (0x90000000 | (immlo << 29) | (immhi << 5) | rd);
}

static inline uint32_t arm64_add_imm(int rd, int rn, uint16_t imm) {
  // ADD Rd, Rn, #imm
  return (0x91000000 | ((uint32_t)imm << 10) | (rn << 5) | rd);
}

static inline uint32_t arm64_nop() {
  return 0xD503201F;
}

// ============== ARM64 随机化跳转 ==============

size_t StealthTrampoline::GenArm64JumpRandomized(uint8_t *buf, uintptr_t target, int reg) {
  if (!initialized_) Init();

  uint32_t *insns = (uint32_t *)buf;
  size_t count = 0;

  // 随机选择跳转方式
  Arm64JumpMethod method = (Arm64JumpMethod)(rand() % (int)kArm64MethodCount);

  switch (method) {
    case kArm64LdrBr: {
      // LDR Xn, [PC, #8]; BR Xn; .quad target
      insns[count++] = arm64_ldr_pc_offset(reg, 8);
      insns[count++] = arm64_br(reg);
      *(uint64_t *)&insns[count] = (uint64_t)target;
      count += 2; // 16 bytes for the address
      break;
    }

    case kArm64AdrpAddBr: {
      // ADRP Xn, #hi; ADD Xn, Xn, #lo; BR Xn
      // 注意: 这要求目标地址在 ADRP 可达范围内
      // ADRP 范围: +/-4GB
      insns[count++] = arm64_adrp(reg, (int64_t)target);
      insns[count++] = arm64_add_imm(reg, reg, (uint16_t)(target & 0xFFF));
      insns[count++] = arm64_br(reg);
      break;
    }

    case kArm64MovkBr: {
      // MOVZ Xn, #lo16; MOVK Xn, #hi16, LSL#16; MOVK Xn, #hi32, LSL#32; BR Xn
      uint64_t addr = (uint64_t)target;
      insns[count++] = arm64_movz(reg, (uint16_t)(addr & 0xFFFF), 0);
      insns[count++] = arm64_movk(reg, (uint16_t)((addr >> 16) & 0xFFFF), 16);
      insns[count++] = arm64_movk(reg, (uint16_t)((addr >> 32) & 0xFFFF), 32);
      insns[count++] = arm64_br(reg);
      break;
    }

    case kArm64AdrpLdrBr:
    default: {
      // 回退到最可靠的 LDR+BR 方式
      insns[count++] = arm64_ldr_pc_offset(reg, 8);
      insns[count++] = arm64_br(reg);
      *(uint64_t *)&insns[count] = (uint64_t)target;
      count += 2;
      break;
    }
  }

  return count * sizeof(uint32_t);
}

size_t StealthTrampoline::GenArm64Trampoline(uint8_t *buf, uintptr_t from, uintptr_t to) {
  if (!initialized_) Init();

  size_t total = 0;
  uint32_t *insns = (uint32_t *)buf;

  // 随机选择临时寄存器
  int reg = RandomTempReg();

  // 随机添加前置伪装指令（不会影响逻辑的指令）
  int decoy_count = rand() % 3; // 0-2 条伪装指令
  for (int i = 0; i < decoy_count; i++) {
    // 伪装指令类型:
    // NOP (最简单)
    // MOV Xn, Xn (等效于 NOP)
    // ADD Xn, Xn, #0 (等效于 NOP)
    int decoy_type = rand() % 3;
    switch (decoy_type) {
      case 0: insns[total / 4] = arm64_nop(); break;
      case 1: insns[total / 4] = 0xAA0003E0 | (reg << 5) | reg; break; // MOV Xn, Xn
      case 2: insns[total / 4] = arm64_add_imm(reg, reg, 0); break;
    }
    total += 4;
  }

  // 生成跳转指令
  total += GenArm64JumpRandomized(buf + total, to, reg) / sizeof(uint32_t) * sizeof(uint32_t);

  return total;
}

// ============== ARM32 / Thumb 隐形跳板 ==============

size_t StealthTrampoline::GenArmTrampoline(uint8_t *buf, uintptr_t from, uintptr_t to) {
  uint32_t *insns = (uint32_t *)buf;

  // ARM32: LDR PC, [PC, #offset] 模式
  // 随机化: 使用不同的寄存器作为中转
  // 但 ARM32 的 LDR PC 是最可靠的跳转方式

  insns[0] = 0xE59FF000 | (4);  // LDR PC, [PC, #4]
  insns[1] = (uint32_t)to;       // 目标地址

  return 8; // 2 * 4 字节
}

size_t StealthTrampoline::GenThumbTrampoline(uint8_t *buf, uintptr_t from, uintptr_t to) {
  uint16_t *insns = (uint16_t *)buf;

  // Thumb: 2 字节对齐
  // 标准方式: LDR.W PC, [PC, #0]; .long target
  // 增强方式: 使用 PUSH/POP 技巧

  // 方法1 (标准但增强):
  // ADD.W PC, PC, #0 等 (不推荐，不自然)

  // 方法2 (更自然):
  // PUSH {R0}; LDR R0, [PC, #4]; MOV PC, R0; POP {R0}
  // 但这修改了 R0，不安全

  // 方法3 (Thumb-2 LDR.W):
  insns[0] = 0xF8DF;       // LDR.W PC, [PC, #0]
  insns[1] = 0xF000;       // (编码偏移=0)
  *(uint32_t *)&insns[2] = (uint32_t)to;

  return 8; // 2 + 2 + 4 字节
}

// ============== 分散式跳板 ==============

size_t StealthTrampoline::GenScatteredTrampoline(uint8_t *buf, uintptr_t from,
                                                   uintptr_t to,
                                                   size_t max_single_jump) {
  // 将一条长跳转拆分为多条短跳转
  // 每个短跳转跳到下一个代码洞穴
  // 这样避免了一个大的跳板区域被扫描到

  // 例如: 原始跳转 A -> D
  // 拆分为: A -> B -> C -> D
  // B 和 C 位于不同的代码洞穴中

  // 简化实现: 只支持 ARM64
#if defined(__aarch64__)
  // 每段跳转使用 B 指令 (相对跳转，范围 +/-128MB)
  // 如果超出范围，使用 LDR+BR

  size_t total = 0;
  uintptr_t current = from;

  while (true) {
    int64_t distance = (int64_t)to - (int64_t)current;
    if (llabs(distance) < (128 * 1024 * 1024)) {
      // 在 B 指令范围内，直接跳转
      uint32_t *insn = (uint32_t *)(buf + total);
      uint32_t imm26 = ((uint32_t)(distance >> 2)) & 0x3FFFFFF;
      *insn = 0x14000000 | imm26; // B #distance
      total += 4;
      break;
    } else {
      // 不在 B 范围内，需要 LDR+BR
      total += GenArm64JumpRandomized(buf + total, to, RandomTempReg());
      break;
    }
  }

  return total;
#else
  return GenArmTrampoline(buf, from, to);
#endif
}

// ============== 伪装指令 ==============

size_t StealthTrampoline::AddDecoyInstructions(uint8_t *buf, size_t current_size,
                                                  size_t target_size) {
  if (!initialized_) Init();

  size_t padding = target_size - current_size;
  if (padding == 0) return current_size;

#if defined(__aarch64__)
  uint32_t *insns = (uint32_t *)(buf + current_size);
  size_t count = 0;

  while (count * 4 < padding) {
    int decoy = rand() % 4;
    switch (decoy) {
      case 0: insns[count] = arm64_nop(); break;
      case 1: {
        // MOV Xn, Xn
        int reg = RandomTempReg();
        insns[count] = 0xAA0003E0 | (reg << 5) | reg;
        break;
      }
      case 2: {
        // ADD Xn, Xn, #0
        int reg = RandomTempReg();
        insns[count] = arm64_add_imm(reg, reg, 0);
        break;
      }
      case 3: {
        // SUB Xn, Xn, #0
        int reg = RandomTempReg();
        insns[count] = 0xD1000000 | (0 << 10) | (reg << 5) | reg;
        break;
      }
    }
    count++;
  }

  return current_size + count * 4;
#else
  // 其他架构: 填充 NOP
  memset(buf + current_size, 0, padding);
  return target_size;
#endif
}

// ============== 原始函数重定位 ==============

size_t StealthTrampoline::GenRelocatedCode(uint8_t *buf, uintptr_t orig_func,
                                             size_t patch_size,
                                             uintptr_t *orig_instructions) {
  // 类似 Substrate 的做法:
  // 1. 复制被覆盖的原始指令
  // 2. 修正 PC-Relative 指令
  // 3. 在末尾添加跳回原函数+偏移的指令

  // 这个功能 Dobby 已经通过 InstructionRelocation 模块实现了
  // 但我们的版本增加了伪装指令

  if (!orig_instructions) return 0;

  // 复制原始指令
  memcpy(buf, orig_instructions, patch_size);

  // 在末尾添加跳回指令
  size_t total = patch_size;
  uintptr_t return_addr = orig_func + patch_size;

#if defined(__aarch64__)
  total += GenArm64JumpRandomized(buf + total, return_addr, RandomTempReg());
#endif

  return total;
}

} // namespace dobby_stealth
