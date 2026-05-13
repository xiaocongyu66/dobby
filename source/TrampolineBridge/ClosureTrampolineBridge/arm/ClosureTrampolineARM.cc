#include "platform_detect_macro.h"
#if defined(TARGET_ARCH_ARM)

#include "dobby/dobby_internal.h"
#include "core/assembler/assembler-arm.h"
#include "TrampolineBridge/ClosureTrampolineBridge/ClosureTrampoline.h"

using namespace zz;
using namespace zz::arm;

asm_func_t get_closure_bridge_addr();

ClosureTrampoline *GenerateClosureTrampoline(void *carry_data, void *carry_handler) {
  auto closure_tramp = new ClosureTrampoline(CLOSURE_TRAMPOLINE_ARM, CodeMemBlock{}, carry_data, carry_handler);

#define _ turbo_assembler_.
  TurboAssembler turbo_assembler_(0);

  PseudoLabel entry_label(0);
  PseudoLabel forward_bridge_label(0);

  _ Ldr(r12, &entry_label);
  _ Ldr(pc, &forward_bridge_label);
  _ bindLabel(&entry_label);
  _ EmitAddress((uint32_t)(uintptr_t)closure_tramp);
  _ bindLabel(&forward_bridge_label);
  _ EmitAddress((uint32_t)(uintptr_t)get_closure_bridge_addr());

  auto tramp_block = AssemblerCodeBuilder::FinalizeFromTurboAssembler(&turbo_assembler_);
  closure_tramp->buffer = tramp_block;
  return closure_tramp;
}

#endif
