#pragma once

#include "MemoryAllocator/CodeMemBuffer.h"

// Legacy ARM/Thumb emitter compatibility layer.
// Some ARMv7 code paths still include CodeBuffer/code_buffer_arm.h and
// expect these instruction aliases plus a CodeBuffer type. Keep this small
// wrapper so Android armeabi-v7a can compile without carrying the old, removed
// CodeBuffer implementation.
struct CodeBuffer : public CodeMemBuffer {
  uint32_t GetBufferSize() const {
    return const_cast<CodeBuffer *>(this)->size();
  }

  uint32_t buffer_size() const {
    return const_cast<CodeBuffer *>(this)->size();
  }

  void Emit16(uint16_t value) {
    EmitThumb1Inst(value);
  }
};
