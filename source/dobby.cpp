#include "dobby.h"
#include "dobby/common.h"
#include "Interceptor.h"
#include "InterceptRouting/InlineHookRouting.h"
#include "InterceptRouting/InstrumentRouting.h"
#include "InterceptRouting/NearBranchTrampoline/NearBranchTrampoline.h"
#include "TrampolineBridge/ClosureTrampolineBridge/common_bridge_handler.h"
#include "MemoryAllocator/NearMemoryAllocator.h"
#include "Runtime/HookRuntime.h"
#include <stdint.h>

__attribute__((constructor)) static void ctor() {
  DEBUG_LOG("================================");
  DEBUG_LOG("Dobby");
  DEBUG_LOG("dobby in debug log mode, disable with cmake flag \"-DDOBBY_DEBUG=OFF\"");
  DEBUG_LOG("================================");
}

#ifndef __DOBBY_BUILD_VERSION__
#define __DOBBY_BUILD_VERSION__ "Dobby-unknown"
#endif

PUBLIC const char *DobbyGetVersion() {
  return __DOBBY_BUILD_VERSION__;
}

namespace {

bool is_valid_handle(DobbyHookHandle *handle) {
  return handle && handle->magic == kDobbyHookHandleMagic;
}

int destroy_entry_locked(Interceptor::Entry *entry) {
  if (!entry) {
    return -1;
  }

  const addr_t target = entry->addr;
  if (entry->origin_code_) {
    entry->restore_orig_code();
  }
  if (entry->routing) {
    delete entry->routing;
    entry->routing = nullptr;
  }
  dobby::HookRuntime::Shared().Remove((void *)target);
  delete entry;
  return 0;
}

} // namespace

PUBLIC int DobbyDestroy(void *address) {
  __FUNC_CALL_TRACE__();
  if (!address) {
    ERROR_LOG("address is 0x0");
    return -1;
  }

  features::arm_thumb_fix_addr(address);
  features::apple::arm64e_pac_strip(address);

  std::lock_guard<std::mutex> interceptor_lock(gInterceptor.mutex_);
  auto entry = gInterceptor.remove((addr_t)address);
  return destroy_entry_locked(entry);
}

PUBLIC int DobbyUnhook(DobbyHookHandle *handle) {
  __FUNC_CALL_TRACE__();
  if (!is_valid_handle(handle)) {
    ERROR_LOG("invalid hook handle");
    return -1;
  }

  if (!handle->active || !handle->entry) {
    return 0;
  }

  std::lock_guard<std::mutex> interceptor_lock(gInterceptor.mutex_);
  auto *entry_from_handle = static_cast<Interceptor::Entry *>(handle->entry);
  auto entry = gInterceptor.remove((addr_t)handle->target);
  if (!entry) {
    handle->active = false;
    handle->entry = nullptr;
    return 0;
  }
  if (entry != entry_from_handle) {
    gInterceptor.add(entry);
    ERROR_LOG("hook handle does not match active entry");
    return -1;
  }

  return destroy_entry_locked(entry);
}

PUBLIC int DobbyDestroyHandle(DobbyHookHandle *handle) {
  __FUNC_CALL_TRACE__();
  if (!is_valid_handle(handle)) {
    ERROR_LOG("invalid hook handle");
    return -1;
  }

  int ret = DobbyUnhook(handle);
  handle->magic = 0;
  delete handle;
  return ret;
}

PUBLIC void *DobbyHookHandleTarget(DobbyHookHandle *handle) {
  if (!is_valid_handle(handle)) {
    return nullptr;
  }
  return (void *)handle->target;
}

PUBLIC void *DobbyHookHandleOrigin(DobbyHookHandle *handle) {
  if (!is_valid_handle(handle)) {
    return nullptr;
  }
  return handle->origin;
}

PUBLIC int DobbyHookHandleIsActive(DobbyHookHandle *handle) {
  if (!is_valid_handle(handle)) {
    return 0;
  }
  return handle->active ? 1 : 0;
}

PUBLIC void dobby_set_options(bool enable_near_trampoline, dobby_alloc_near_code_callback_t alloc_near_code_callback) {
  dobby_set_near_trampoline(enable_near_trampoline);
  dobby_register_alloc_near_code_callback(alloc_near_code_callback);
}

PUBLIC uintptr_t placeholder() {
  uintptr_t x = 0;
  x += (uintptr_t)&DobbyHook;
  x += (uintptr_t)&DobbyInstrument;
  x += (uintptr_t)&dobby_set_near_trampoline;
  x += (uintptr_t)&common_closure_bridge_handler;
  x += (uintptr_t)&dobby_register_alloc_near_code_callback;
  return x;
}
