#pragma once

#include "MemoryAllocator/CodeMemBuffer.h"
#include "MemoryAllocator/NearMemoryAllocator.h"

enum trampoline_type_t {
  TRAMPOLINE_UNKNOWN = 0,
  TRAMPOLINE_ARM64_B_XXX,
  TRAMPOLINE_ARM64_B_XXX_AND_FORWARD_TRAMP,
  TRAMPOLINE_ARM64_ADRP_ADD_BR,
  TRAMPOLINE_ARM64_LDR_BR,
  TRAMPOLINE_ARM_LDR_PC,


  CLOSURE_TRAMPOLINE_ARM64,
  CLOSURE_TRAMPOLINE_ARM,
  FORWARD_TRAMPOLINE_ARM64,

  TRAMPOLINE_X64_JMP,
  CLOSEURE_TRAMPOLINE_X64,
};

enum trampoline_buffer_ownership_t {
  TRAMPOLINE_BUFFER_NONE = 0,
  TRAMPOLINE_BUFFER_HEAP = 1,
  TRAMPOLINE_BUFFER_EXEC_ALLOCATOR = 2,
  TRAMPOLINE_BUFFER_NEAR_EXEC_ALLOCATOR = 3,
  TRAMPOLINE_BUFFER_NEAR_DATA_ALLOCATOR = 4,
};

struct Trampoline {
  int type;
  CodeMemBlock buffer;
  trampoline_buffer_ownership_t buffer_ownership = TRAMPOLINE_BUFFER_NONE;

  Trampoline *forward_trampoline = nullptr;
  stl::vector<CodeMemBlock> owned_near_code_blocks;
  stl::vector<DataMemBlock> owned_near_data_blocks;

  Trampoline() : type(0), buffer() {
  }

  Trampoline(int type, CodeMemBlock buffer,
             trampoline_buffer_ownership_t ownership = TRAMPOLINE_BUFFER_HEAP)
      : type(type), buffer(buffer), buffer_ownership(ownership) {
  }

  Trampoline(int type, CodeMemBlock buffer, Trampoline *forward,
             trampoline_buffer_ownership_t ownership = TRAMPOLINE_BUFFER_HEAP)
      : type(type), buffer(buffer), buffer_ownership(ownership), forward_trampoline(forward) {
  }

  virtual ~Trampoline() {
    if (forward_trampoline) {
      delete forward_trampoline;
      forward_trampoline = nullptr;
    }

    release_buffer();

    for (auto block : owned_near_code_blocks) {
      gNearMemoryAllocator.freeNearCodeBlock(block);
    }
    owned_near_code_blocks.clear();

    for (auto block : owned_near_data_blocks) {
      gNearMemoryAllocator.freeNearDataBlock(block);
    }
    owned_near_data_blocks.clear();
  }

  void set_buffer(CodeMemBlock in_buffer, trampoline_buffer_ownership_t ownership) {
    release_buffer();
    buffer = in_buffer;
    buffer_ownership = ownership;
  }

  void own_near_code_block(CodeMemBlock block) {
    if (block.addr() != 0 && block.size != 0) {
      owned_near_code_blocks.push_back(block);
    }
  }

  void own_near_data_block(DataMemBlock block) {
    if (block.addr() != 0 && block.size != 0) {
      owned_near_data_blocks.push_back(block);
    }
  }

  addr_t addr() {
    return buffer.addr();
  }

  addr_t size() {
    return buffer.size;
  }

  addr_t forward_addr() {
    return forward_trampoline ? forward_trampoline->addr() : 0;
  }

  addr_t forward_size() {
    return forward_trampoline ? forward_trampoline->size() : 0;
  }

private:
  void release_buffer() {
    if (buffer.addr() == 0 || buffer.size == 0) {
      buffer = CodeMemBlock{};
      buffer_ownership = TRAMPOLINE_BUFFER_NONE;
      return;
    }

    switch (buffer_ownership) {
    case TRAMPOLINE_BUFFER_HEAP:
      operator delete((void *)buffer.addr());
      break;
    case TRAMPOLINE_BUFFER_EXEC_ALLOCATOR:
      gMemoryAllocator.freeExecBlock(buffer);
      break;
    case TRAMPOLINE_BUFFER_NEAR_EXEC_ALLOCATOR:
      gNearMemoryAllocator.freeNearCodeBlock(buffer);
      break;
    case TRAMPOLINE_BUFFER_NEAR_DATA_ALLOCATOR:
      gNearMemoryAllocator.freeNearDataBlock(buffer);
      break;
    case TRAMPOLINE_BUFFER_NONE:
    default:
      break;
    }

    buffer = CodeMemBlock{};
    buffer_ownership = TRAMPOLINE_BUFFER_NONE;
  }
};
