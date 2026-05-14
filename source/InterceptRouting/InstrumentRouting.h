#pragma once

#include "dobby/common.h"
#include "InterceptRouting/InterceptRouting.h"
#include "TrampolineBridge/ClosureTrampolineBridge/ClosureTrampoline.h"
#include "Runtime/HookRuntime.h"

struct InstrumentRouting : InterceptRouting {
  ClosureTrampoline *instrument_tramp = nullptr;

  InstrumentRouting(Interceptor::Entry *entry, dobby_instrument_callback_t pre_handler) : InterceptRouting(entry) {
  }

  ~InstrumentRouting() {
    if (instrument_tramp) {
      delete instrument_tramp;
    }
  }

  addr_t TrampolineTarget() override {
    return instrument_tramp->addr();
  }

  void GenerateInstrumentClosureTrampoline() {
    __FUNC_CALL_TRACE__();
    instrument_tramp = ::GenerateInstrumentClosureTrampoline(entry);
    if (!instrument_tramp) {
      error = 1;
    }
  }

  void BuildRouting() {
    __FUNC_CALL_TRACE__();

    GenerateInstrumentClosureTrampoline();

    if (!GenerateTrampoline()) {
      return;
    }

    GenerateRelocatedCode();

    BackupOriginCode();
  }
};

PUBLIC inline int DobbyInstrument(void *address, dobby_instrument_callback_t pre_handler) {
  __FUNC_CALL_TRACE__();
  if (!address) {
    ERROR_LOG("address is 0x0.");
    return -1;
  }

  features::apple::arm64e_pac_strip(address);
  features::android::make_memory_readable(address, 4);

  DEBUG_LOG("----- [DobbyInstrument: %p] -----", address);

  std::lock_guard<std::mutex> interceptor_lock(gInterceptor.mutex_);

  auto entry = gInterceptor.find((addr_t)address);
  if (entry) {
    ERROR_LOG("%s already been instrumented.", address);
    return -1;
  }

  entry = new Interceptor::Entry((addr_t)address);
  entry->id = gInterceptor.next_entry_id_++;
  entry->pre_handler = pre_handler;

  auto routing = new InstrumentRouting(entry, pre_handler);
  routing->BuildRouting();
  if (routing->error) {
    ERROR_LOG("build routing error.");
    delete routing;
    delete entry;
    return -1;
  }

  entry->routing = routing;
  routing->Active();
  if (routing->error) {
    ERROR_LOG("routing active failed.");
    if (entry->origin_code_) {
      entry->restore_orig_code();
    }
    delete routing;
    delete entry;
    return -1;
  }

  gInterceptor.add(entry);

  dobby::HookRecord runtime_record;
  runtime_record.target = (void *)address;
  runtime_record.replace = nullptr;
  runtime_record.backup = nullptr;
  runtime_record.trampoline = reinterpret_cast<void *>(entry->relocated.addr());
  runtime_record.backend = dobby::HookBackend::Inline;
  runtime_record.state = dobby::HookState::Enabled;
  runtime_record.enabled = true;
  runtime_record.patch_addr = entry->patched.addr();
  runtime_record.patch_size = entry->patched.size;
  dobby::HookRuntime::Shared().Upsert(runtime_record);

  return 0;
}
