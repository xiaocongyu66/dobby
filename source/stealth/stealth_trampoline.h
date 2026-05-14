#pragma once

#include <cstdint>
#include <vector>

namespace dobby_stealth {

// 隐形跳板: 增强 Dobby 的 trampoline 使其更难被检测
//
// Dobby 默认 trampoline 的问题:
// 1. ARM64: 使用 LDR X17, [PC, #8]; BR X17 模式，这是已知特征
// 2. ARM32: 使用 LDR PC, [PC, #offset]，也是已知特征
// 3. Thumb: 使用 LDR.W PC, [PC, #0]，特征明显
// 4. 跳板代码通常有固定模式，容易被特征扫描发现
//
// 增强方案:
// 1. 随机化跳板指令序列（等效指令替换）
// 2. 使用不同的寄存器
// 3. 添加伪装指令（合法但不影响逻辑的指令）
// 4. 分散跳板到多个代码洞穴
// 5. 使用更自然的跳转模式（如函数调用习惯）
struct StealthTrampoline {
  // 生成 ARM64 隐形跳板
  // 随机选择跳转方式和寄存器
  static size_t GenArm64Trampoline(uint8_t *buf, uintptr_t from, uintptr_t to);

  // 生成 ARM32 隐形跳板
  static size_t GenArmTrampoline(uint8_t *buf, uintptr_t from, uintptr_t to);

  // 生成 Thumb 隐形跳板
  static size_t GenThumbTrampoline(uint8_t *buf, uintptr_t from, uintptr_t to);

  // 生成分散式跳板（将长跳转拆分为多个短跳转）
  static size_t GenScatteredTrampoline(uint8_t *buf, uintptr_t from, uintptr_t to,
                                        size_t max_single_jump);

  // 为跳板添加伪装指令
  static size_t AddDecoyInstructions(uint8_t *buf, size_t current_size,
                                      size_t target_size);

  // 生成原始函数的跳转恢复代码（类似 Substrate 的 result 函数）
  // 这个跳板先执行被覆盖的原始指令，然后跳回原函数+偏移
  static size_t GenRelocatedCode(uint8_t *buf, uintptr_t orig_func,
                                  size_t patch_size, uintptr_t *orig_instructions);

  // 初始化随机种子
  static void Init();

private:
  static bool initialized_;
  static uint32_t random_seed_;

  // ARM64 可用的临时寄存器 (X0-X28 中非参数寄存器)
  static const int ARM64_TEMP_REGS[];
  static const int ARM64_TEMP_REG_COUNT;

  // 随机选择一个临时寄存器
  static int RandomTempReg();

  // 随机生成一个 ARM64 跳转序列
  // 返回写入的字节数
  static size_t GenArm64JumpRandomized(uint8_t *buf, uintptr_t target, int reg);

  // 枚举 ARM64 不同的跳转方式
  enum Arm64JumpMethod {
    kArm64LdrBr = 0,        // LDR Xn, [PC,#8]; BR Xn (Dobby 默认)
    kArm64AdrpAddBr,         // ADRP Xn, #hi; ADD Xn, Xn, #lo; BR Xn
    kArm64AdrpLdrBr,         // ADRP Xn, #hi; LDR Xn, [Xn, #lo]; BR Xn
    kArm64MovkBr,            // MOVZ + MOVK + BR
    kArm64MovkBrx,           // MOVZ + MOVK + BR Xn (不同寄存器)
    kArm64MethodCount
  };
};

} // namespace dobby_stealth
