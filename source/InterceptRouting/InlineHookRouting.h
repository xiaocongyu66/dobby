#pragma once

#include "dobby/common.h"
#include "InterceptRouting/InterceptRouting.h"
#include "TrampolineBridge/ClosureTrampolineBridge/ClosureTrampoline.h"
#include "Runtime/HookRuntime.h"

struct InlineHookRouting : InterceptRouting {
  addr_t fake_func;

  InlineHookRouting(Interceptor::Entry *entry, addr_t fake_func) : InterceptRouting(entry), fake_func(fake_func) {
  }

  ~InlineHookRouting() = default;

  addr_t TrampolineTarget() override {
    return fake_func;
  }

  void BuildRouting() {
    __FUNC_CALL_TRACE__();

    if (!GenerateTrampoline()) {
      return;
    }

    GenerateRelocatedCode();

    BackupOriginCode();
  }
};

PUBLIC inline int DobbyHookEx(void *address, void *fake_func, void **out_origin_func, DobbyHookHandle **out_handle) {
  __FUNC_CALL_TRACE__();
  if (out_handle) {
    *out_handle = nullptr;
  }
  if (!address || !fake_func) {
    ERROR_LOG("invalid DobbyHookEx argument: address=%p fake_func=%p", address, fake_func);
    return -1;
  }

  features::apple::arm64e_pac_strip(address);
  features::apple::arm64e_pac_strip(fake_func);
  features::android::make_memory_readable(address, 4);

  DEBUG_LOG("----- [DobbyHook: %p] -----", address);

  std::lock_guard<std::mutex> interceptor_lock(gInterceptor.mutex_);

  auto entry = gInterceptor.find((addr_t)address);
  if (entry) {
    ERROR_LOG("%p already been hooked.", address);
    return -1;
  }

  entry = new Interceptor::Entry((addr_t)address);
  entry->id = gInterceptor.next_entry_id_++;
  entry->fake_func_addr = (addr_t)fake_func;

  auto routing = new InlineHookRouting(entry, (addr_t)fake_func);
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

  void *origin_func = (void *)entry->relocated.addr();
  features::apple::arm64e_pac_strip_and_sign(origin_func);
  if (out_origin_func) {
    *out_origin_func = origin_func;
  }

  DobbyHookHandle *handle = nullptr;
  if (out_handle) {
    handle = new DobbyHookHandle();
    handle->id = entry->id;
    handle->target = (addr_t)address;
    handle->origin = origin_func;
    handle->active = true;
    handle->entry = entry;
    entry->handle = handle;
    *out_handle = handle;
  }

  gInterceptor.add(entry);

  dobby::HookRecord runtime_record;
  runtime_record.target = (void *)address;
  runtime_record.replace = fake_func;
  runtime_record.backup = out_origin_func ? *out_origin_func : nullptr;
  runtime_record.trampoline = reinterpret_cast<void *>(entry->relocated.addr());
  runtime_record.backend = dobby::HookBackend::Inline;
  runtime_record.state = dobby::HookState::Enabled;
  runtime_record.enabled = true;
  runtime_record.patch_addr = entry->patched.addr();
  runtime_record.patch_size = entry->patched.size;
  dobby::HookRuntime::Shared().Upsert(runtime_record);

  return 0;
}

PUBLIC inline int DobbyHook(void *address, void *fake_func, void **out_origin_func) {
  return DobbyHookEx(address, fake_func, out_origin_func, nullptr);
}
