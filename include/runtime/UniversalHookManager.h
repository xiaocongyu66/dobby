#pragma once

#include "dobby.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dobby {

struct DeferredHookDescriptor {
  std::string image_name;
  std::string symbol_name;
  void *replace_call = nullptr;
  void **origin_call = nullptr;
  std::uint64_t retry_interval_ms = DOBBY_AUTOHOOK_DEFAULT_RETRY_MS;
  std::uint64_t timeout_ms = 0;
  std::uint64_t start_delay_ms = DOBBY_AUTOHOOK_DEFAULT_START_DELAY_MS;
  std::uint32_t flags = DOBBY_AUTOHOOK_WAIT_MODULE | DOBBY_AUTOHOOK_WAIT_SYMBOL | DOBBY_AUTOHOOK_RETRY |
                        DOBBY_AUTOHOOK_DELAY_FIRST;
  std::uintptr_t offset = 0;
  void *target_addr = nullptr;
  int backend = DOBBY_HOOK_BACKEND_AUTO;
  dobby_autohook_callback_t callback = nullptr;
  void *user_data = nullptr;
  bool require_executable_mapping = true;
};

class UniversalHookManager {
public:
  static UniversalHookManager &Shared();

  void Configure(const DobbyRuntimeOptions *options);
  bool Start();
  bool Schedule(const DeferredHookDescriptor &descriptor);
  int InstallBlocking(DeferredHookDescriptor descriptor, std::uint64_t timeout_ms);
  void Shutdown(bool clear_pending = true);
  std::size_t PendingCount();

private:
  UniversalHookManager() = default;
  ~UniversalHookManager();
  UniversalHookManager(const UniversalHookManager &) = delete;
  UniversalHookManager &operator=(const UniversalHookManager &) = delete;

  enum class TaskStatus : int {
    Pending = DOBBY_AUTOHOOK_STATUS_PENDING,
    Installed = DOBBY_AUTOHOOK_STATUS_INSTALLED,
    Timeout = DOBBY_AUTOHOOK_STATUS_TIMEOUT,
    Failed = DOBBY_AUTOHOOK_STATUS_FAILED,
    Cancelled = DOBBY_AUTOHOOK_STATUS_CANCELLED,
  };

  struct Task {
    DeferredHookDescriptor descriptor;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point last_attempt_at;
    std::atomic<int> status{static_cast<int>(TaskStatus::Pending)};
    std::atomic<bool> callback_sent{false};
    void *resolved_target = nullptr;
    void *origin_func = nullptr;
  };

  static DeferredHookDescriptor Normalize(const DeferredHookDescriptor &descriptor,
                                          std::uint32_t default_flags,
                                          std::uint64_t default_retry_ms,
                                          std::uint64_t default_timeout_ms,
                                          std::uint64_t default_start_delay_ms);
  TaskStatus TryInstall(Task &task, bool ignore_retry_interval);
  void NotifyCallback(Task &task, TaskStatus status);
  void WorkerLoop();

  std::atomic<bool> running_{false};
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::shared_ptr<Task>> tasks_;

  std::uint32_t default_flags_{DOBBY_AUTOHOOK_WAIT_MODULE | DOBBY_AUTOHOOK_WAIT_SYMBOL | DOBBY_AUTOHOOK_RETRY |
                               DOBBY_AUTOHOOK_DELAY_FIRST};
  std::uint64_t default_retry_ms_{DOBBY_AUTOHOOK_DEFAULT_RETRY_MS};
  std::uint64_t default_timeout_ms_{0};
  std::uint64_t default_start_delay_ms_{DOBBY_AUTOHOOK_DEFAULT_START_DELAY_MS};
};

bool DobbyRegisterAutoHook(const char *image_name,
                           const char *symbol_name,
                           void *replace_call,
                           void **origin_call,
                           std::uint64_t retry_interval_ms);

} // namespace dobby
