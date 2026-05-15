#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dobby {

struct DeferredHookDescriptor {
  std::string image_name;
  std::string symbol_name;
  void *replace_call{};
  void **origin_call{};
  uint64_t retry_interval_ms{250};
  uint64_t timeout_ms{0};
  bool auto_retry{true};
  bool require_executable_mapping{true};
  std::function<bool()> predicate;
};

class UniversalHookManager {
public:
  static UniversalHookManager &Shared();

  bool Schedule(const DeferredHookDescriptor &descriptor);
  void Shutdown();

private:
  UniversalHookManager();
  ~UniversalHookManager();

  struct Task {
    DeferredHookDescriptor descriptor;
    std::atomic<bool> installed{false};
    std::chrono::steady_clock::time_point created_at;
  };

  bool TryInstall(Task &task);
  void WorkerLoop();

  std::atomic<bool> running_{true};
  std::thread worker_;
  std::mutex mutex_;
  std::vector<std::shared_ptr<Task>> tasks_;
};

bool DobbyRegisterAutoHook(const char *image_name,
                           const char *symbol_name,
                           void *replace_call,
                           void **origin_call,
                           uint64_t retry_interval_ms);

} // namespace dobby
