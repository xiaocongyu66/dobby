#pragma once

#include "dobby/common.h"
#include "pseudo_label.h"

namespace zz {
struct ExternalReference {
  void *address;

  explicit ExternalReference(void *address) : address(address) {
    address = pac_strip(address);
  }
};

struct AssemblerBase {
  addr_t fixed_addr;
  void *realized_addr_;
  CodeMemBuffer code_buffer_;

  // Legacy ARM/IA32 backends still use a pointer named buffer_. Keep it as an
  // alias to the embedded buffer by default, while allowing those backends to
  // replace it with their arch-specific CodeBuffer wrapper.
  CodeMemBuffer *buffer_;
  stl::vector<RelocDataLabel *> data_labels;

  explicit AssemblerBase(addr_t fixed_addr) {
    this->fixed_addr = fixed_addr;
    this->realized_addr_ = (void *)fixed_addr;
    this->buffer_ = &code_buffer_;
  }

  explicit AssemblerBase(void *fixed_addr) : AssemblerBase((addr_t)fixed_addr) {
  }

  ~AssemblerBase() = default;

  size_t pc_offset() {
    return code_buffer()->size();
  }

  void set_fixed_addr(addr_t in_fixed_addr) {
    this->fixed_addr = in_fixed_addr;
    this->realized_addr_ = (void *)in_fixed_addr;
  }

  void SetRealizedAddress(void *address) {
    set_fixed_addr((addr_t)address);
  }

  void *GetRealizedAddress() {
    return realized_addr_;
  }

  CodeMemBuffer *code_buffer() {
    return buffer_ ? buffer_ : &code_buffer_;
  }

  // --- label

  RelocDataLabel *createDataLabel(uint64_t data) {
    auto data_label = new RelocDataLabel(data);
    data_labels.push_back(data_label);
    return data_label;
  }

  void bindLabel(PseudoLabel *label) {
    label->bind_to(pc_offset());
    if (label->has_confused_instructions()) {
      label->link_confused_instructions(code_buffer());
    }
  }

  void relocDataLabels() {
    for (auto *data_label : data_labels) {
      bindLabel(data_label);
      code_buffer()->emit(data_label->data_, data_label->data_size_);
    }
  }
};

} // namespace zz