#include "platform_detect_macro.h"
#if defined(TARGET_ARCH_X64)

#include "dobby/dobby_internal.h"

#include "core/assembler/assembler-x64.h"
#include "core/codegen/codegen-x64.h"

#include "MemoryAllocator/NearMemoryAllocator.h"
#include "InstructionRelocation/x64/InstructionRelocationX64.h"
#include "InterceptRouting/RoutingPlugin.h"

using namespace zz::x64;

static DataMemBlock allocate_indirect_stub(addr_t jmp_insn_addr) {
  uint32_t jmp_near_range = (uint32_t)2 * 1024 * 1024 * 1024;
  auto blk = gNearMemoryAllocator.allocNearDataBlock(sizeof(void *), jmp_insn_addr, jmp_near_range);
  auto stub_addr = blk.start();
  if (stub_addr == 0) {
    ERROR_LOG("Not found near forward stub");
    return {};
  }

  DEBUG_LOG("forward stub: %p, offset: %lld", stub_addr, stub_addr - jmp_insn_addr);
  return blk;
}

Trampoline *GenerateNormalTrampolineBuffer(addr_t from, addr_t to) {
  __FUNC_CALL_TRACE__();
  TurboAssembler turbo_assembler_(from);
#undef _
#define _ turbo_assembler_. // NOLINT: clang-tidy

  // Prefer the compact RIP-relative indirect jump when a nearby data stub can
  // be allocated. If that is not possible, fall back to an absolute jump that
  // does not need auxiliary near memory.
  auto jump_near_next_insn_addr = from + 6;
  auto forward_stub_block = allocate_indirect_stub(jump_near_next_insn_addr);
  addr_t forward_stub = forward_stub_block.addr();
  if (forward_stub != 0) {
    *(addr_t *)forward_stub = to;

    CodeGen codegen(&turbo_assembler_);
    codegen.JmpNearIndirect((addr_t)forward_stub);
  } else {
    DEBUG_LOG("fall back to x64 absolute jump trampoline");
    turbo_assembler_.mov(rax, Immediate((int64_t)to, 64));
    turbo_assembler_.EmitOpcode(0xff);
    turbo_assembler_.EmitModRM(0b11, 0x4, rax.low_bits());
  }

  auto tramp_buffer = turbo_assembler_.code_buffer();
  auto tramp_block = tramp_buffer->dup();
  auto tramp = new Trampoline(TRAMPOLINE_X64_JMP, tramp_block);
  if (forward_stub_block.addr() != 0) {
    tramp->own_near_data_block(forward_stub_block);
  }
  DEBUG_LOG("[trampoline] trampoline addr: %p(temp), %p(real), size: %d", tramp->addr(), from, tramp->size());
  debug_hex_log_buffer((uint8_t *)tramp->addr(), tramp->size());
  return tramp;
}

Trampoline *GenerateNearTrampolineBuffer(addr_t src, addr_t dst) {
  DEBUG_LOG("x64 near branch trampoline enable default");
  return nullptr;
}

#endif