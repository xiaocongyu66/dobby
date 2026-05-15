#include "runtime/UniversalHookManager.h"

#include <chrono>
#include <thread>

#include "dobby.h"

#if defined(__linux__) || defined(__ANDROID__)
#include <dlfcn.h>
#endif

namespace dobby {

UniversalHookManager &UniversalHookManager::Shared() {
  static UniversalHookManager manager;
  return manager;
}

UniversalHookManager::UniversalHookManager() {
  worker_ = std::thread(&UniversalHookManager::WorkerLoop, this);
}

UniversalHookManager::~UniversalHookManager() {
  Shutdown();
}

void UniversalHookManager::Shutdown() {
  running_ = false;
  if (worker_.joinable()) {
    worker_.join();
  }
}

bool UniversalHookManager::Schedule(const DeferredHookDescriptor &descriptor) {
  auto task = std::make_shared<Task>();
  task->descriptor = descriptor;
  task->created_at = std::chrono::steady_clock::now();

  std::lock_guard<std::mutex> lock(mutex_);
  tasks_.push_back(task);
  return true;
}

bool UniversalHookManager::TryInstall(Task &task) {
  if (task.installed) {
    return true;
  }

  if (task.descriptor.predicate && !task.descriptor.predicate()) {
    return false;
  }

#if defined(__linux__) || defined(__ANDROID__)
  void *handle = dlopen(task.descriptor.image_name.c_str(), RTLD_NOW | RTLD_NOLOAD);
  if (!handle) {
    return false;
  }

  void *symbol = dlsym(handle, task.descriptor.symbol_name.c_str());
  if (!symbol) {
    dlclose(handle);
    return false;
  }

  if (DobbyHook(symbol, task.descriptor.replace_call, task.descriptor.origin_call) == 0) {
    task.installed = true;
  }

  dlclose(handle);
#endif

  return task.installed;
}

void UniversalHookManager::WorkerLoop() {
  while (running_) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto &task : tasks_) {
        if (task->installed) {
          continue;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - task->created_at);

        if (task->descriptor.timeout_ms > 0 &&
            elapsed.count() > static_cast<long long>(task->descriptor.timeout_ms)) {
          task->installed = true;
          continue;
        }

        TryInstall(*task);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

bool DobbyRegisterAutoHook(const char *image_name,
                           const char *symbol_name,
                           void *replace_call,
                           void **origin_call,
                           uint64_t retry_interval_ms) {
  DeferredHookDescriptor descriptor;
  descriptor.image_name = image_name ? image_name : "";
  descriptor.symbol_name = symbol_name ? symbol_name : "";
  descriptor.replace_call = replace_call;
  descriptor.origin_call = origin_call;
  descriptor.retry_interval_ms = retry_interval_ms;

  return UniversalHookManager::Shared().Schedule(descriptor);
}

} // namespace dobby
