#include "runtime/UniversalHookManager.h"
#include "dobby/common.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#if defined(__linux__) || defined(__ANDROID__)
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>
#endif

namespace dobby {

namespace {

static const std::uint64_t kWorkerTickMs = 100;

const char *basename_of(const char *path) {
  if (!path)
    return nullptr;
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

bool ends_with(const char *str, const char *suffix) {
  if (!str || !suffix)
    return false;
  const std::size_t str_len = strlen(str);
  const std::size_t suffix_len = strlen(suffix);
  if (suffix_len > str_len)
    return false;
  return strcmp(str + str_len - suffix_len, suffix) == 0;
}

bool image_matches(const char *loaded_name, const char *image_name) {
  if (!image_name || image_name[0] == '\0')
    return true;
  if (!loaded_name || loaded_name[0] == '\0')
    return false;
  if (strcmp(loaded_name, image_name) == 0)
    return true;
  const char *loaded_base = basename_of(loaded_name);
  const char *image_base = basename_of(image_name);
  return (loaded_base && image_base && strcmp(loaded_base, image_base) == 0) || ends_with(loaded_name, image_name);
}

#if defined(__linux__) || defined(__ANDROID__)
struct ModuleSearchCtx {
  const char *image_name = nullptr;
  std::uintptr_t base = 0;
};

std::uintptr_t find_loaded_module_base(const char *image_name) {
  if (!image_name || image_name[0] == '\0')
    return 0;

#if defined(__ANDROID__) && defined(DOBBY_ANDROID_USE_XDL)
  // Android prefers xDL over dl_iterate_phdr. xDL handles linker namespace
  // differences better and can recover full path/module metadata when bionic
  // exposes only partial information.
  const std::uintptr_t xdl_base = DobbyGetLibraryBase(image_name);
  if (xdl_base)
    return xdl_base;
#endif

  ModuleSearchCtx ctx;
  ctx.image_name = image_name;
#if defined(__ANDROID__) && defined(DOBBY_ANDROID_USE_XDL)
  xdl_iterate_phdr(
      [](struct dl_phdr_info *info, size_t size, void *data) -> int {
        (void)size;
        auto *ctx = reinterpret_cast<ModuleSearchCtx *>(data);
        if (!ctx || !info)
          return 0;
        if (!image_matches(info->dlpi_name, ctx->image_name))
          return 0;
        ctx->base = static_cast<std::uintptr_t>(info->dlpi_addr);
        return 1;
      },
      &ctx, XDL_FULL_PATHNAME);
#else
  dl_iterate_phdr(
      [](struct dl_phdr_info *info, size_t size, void *data) -> int {
        (void)size;
        auto *ctx = reinterpret_cast<ModuleSearchCtx *>(data);
        if (!ctx || !info)
          return 0;
        if (!image_matches(info->dlpi_name, ctx->image_name))
          return 0;
        ctx->base = static_cast<std::uintptr_t>(info->dlpi_addr);
        return 1;
      },
      &ctx);
#endif
  return ctx.base;
}

bool is_executable_mapping(void *addr) {
  if (!addr)
    return false;
  FILE *fp = fopen("/proc/self/maps", "r");
  if (!fp)
    return true;

  const auto query = reinterpret_cast<std::uintptr_t>(addr);
  char line[512];
  bool executable = false;
  while (fgets(line, sizeof(line), fp)) {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
    char perms[5] = {0};
    if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) != 3)
      continue;
    if (query >= start && query < end) {
      executable = perms[2] == 'x';
      break;
    }
  }
  fclose(fp);
  return executable;
}
#else
std::uintptr_t find_loaded_module_base(const char *image_name) {
  (void)image_name;
  return 0;
}

bool is_executable_mapping(void *addr) {
  return addr != nullptr;
}
#endif

bool should_use_plt(const DeferredHookDescriptor &descriptor) {
  return descriptor.backend == DOBBY_HOOK_BACKEND_PLT || (descriptor.flags & DOBBY_AUTOHOOK_USE_PLT) != 0;
}

} // namespace

UniversalHookManager &UniversalHookManager::Shared() {
  static UniversalHookManager manager;
  return manager;
}

UniversalHookManager::~UniversalHookManager() {
  Shutdown(true);
}

void UniversalHookManager::Configure(const DobbyRuntimeOptions *options) {
  if (!options)
    return;

  std::lock_guard<std::mutex> lock(mutex_);
  if (options->flags != DOBBY_AUTOHOOK_NONE)
    default_flags_ = options->flags;
  if (options->retry_interval_ms != 0)
    default_retry_ms_ = options->retry_interval_ms;
  default_timeout_ms_ = options->timeout_ms;
  if (options->start_delay_ms != 0)
    default_start_delay_ms_ = options->start_delay_ms;
}

bool UniversalHookManager::Start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return true;
  }

  worker_ = std::thread(&UniversalHookManager::WorkerLoop, this);
  return true;
}

DeferredHookDescriptor UniversalHookManager::Normalize(const DeferredHookDescriptor &descriptor,
                                                       std::uint32_t default_flags,
                                                       std::uint64_t default_retry_ms,
                                                       std::uint64_t default_timeout_ms,
                                                       std::uint64_t default_start_delay_ms) {
  DeferredHookDescriptor out = descriptor;
  if (out.flags == DOBBY_AUTOHOOK_NONE)
    out.flags = default_flags;
  if (out.retry_interval_ms == 0)
    out.retry_interval_ms = default_retry_ms ? default_retry_ms : DOBBY_AUTOHOOK_DEFAULT_RETRY_MS;
  if (out.timeout_ms == 0)
    out.timeout_ms = default_timeout_ms;
  if (out.start_delay_ms == 0 && (out.flags & DOBBY_AUTOHOOK_DELAY_FIRST))
    out.start_delay_ms = default_start_delay_ms;
  if (out.backend == DOBBY_HOOK_BACKEND_AUTO && should_use_plt(out))
    out.backend = DOBBY_HOOK_BACKEND_PLT;
  return out;
}

bool UniversalHookManager::Schedule(const DeferredHookDescriptor &descriptor) {
  if (!descriptor.replace_call)
    return false;
  if (descriptor.symbol_name.empty() && descriptor.offset == 0 && !descriptor.target_addr)
    return false;

  auto task = std::make_shared<Task>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    task->descriptor = Normalize(descriptor, default_flags_, default_retry_ms_, default_timeout_ms_, default_start_delay_ms_);
    task->created_at = std::chrono::steady_clock::now();
    task->last_attempt_at = std::chrono::steady_clock::time_point{};
    tasks_.push_back(task);
  }
  Start();
  cv_.notify_all();
  return true;
}

UniversalHookManager::TaskStatus UniversalHookManager::TryInstall(Task &task, bool ignore_retry_interval) {
  if (task.status.load() != static_cast<int>(TaskStatus::Pending))
    return static_cast<TaskStatus>(task.status.load());

  const auto now = std::chrono::steady_clock::now();
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - task.created_at).count();

  if (task.descriptor.timeout_ms > 0 && elapsed_ms >= static_cast<long long>(task.descriptor.timeout_ms)) {
    task.status.store(static_cast<int>(TaskStatus::Timeout));
    return TaskStatus::Timeout;
  }

  if (task.descriptor.start_delay_ms > 0 && elapsed_ms < static_cast<long long>(task.descriptor.start_delay_ms)) {
    return TaskStatus::Pending;
  }

  if (!ignore_retry_interval && task.descriptor.retry_interval_ms > 0 &&
      task.last_attempt_at != std::chrono::steady_clock::time_point{}) {
    const auto since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - task.last_attempt_at).count();
    if (since_last < static_cast<long long>(task.descriptor.retry_interval_ms))
      return TaskStatus::Pending;
  }
  task.last_attempt_at = now;

  if (!task.descriptor.replace_call) {
    task.status.store(static_cast<int>(TaskStatus::Failed));
    return TaskStatus::Failed;
  }

  void *target = task.descriptor.target_addr;
  int result = -1;

  if (target) {
    if (task.descriptor.require_executable_mapping && !is_executable_mapping(target))
      return TaskStatus::Pending;
    result = DobbyHook(target, task.descriptor.replace_call, task.descriptor.origin_call);
    if (result == 0) {
      task.resolved_target = target;
      task.origin_func = task.descriptor.origin_call ? *task.descriptor.origin_call : nullptr;
      task.status.store(static_cast<int>(TaskStatus::Installed));
      return TaskStatus::Installed;
    }
    return TaskStatus::Failed;
  }

  if (task.descriptor.offset != 0) {
    const std::uintptr_t base = find_loaded_module_base(task.descriptor.image_name.c_str());
    if (!base)
      return TaskStatus::Pending;
    target = reinterpret_cast<void *>(base + task.descriptor.offset);
    if (task.descriptor.require_executable_mapping && !is_executable_mapping(target))
      return TaskStatus::Pending;
    result = DobbyHook(target, task.descriptor.replace_call, task.descriptor.origin_call);
    if (result == 0) {
      task.resolved_target = target;
      task.origin_func = task.descriptor.origin_call ? *task.descriptor.origin_call : nullptr;
      task.status.store(static_cast<int>(TaskStatus::Installed));
      return TaskStatus::Installed;
    }
    return TaskStatus::Failed;
  }

  if (task.descriptor.symbol_name.empty())
    return TaskStatus::Pending;

  if (should_use_plt(task.descriptor)) {
    if (!task.descriptor.image_name.empty() && !find_loaded_module_base(task.descriptor.image_name.c_str()))
      return TaskStatus::Pending;
    result = DobbyAndroidHookSymbolBackend(task.descriptor.image_name.empty() ? nullptr : task.descriptor.image_name.c_str(),
                                          task.descriptor.symbol_name.c_str(), task.descriptor.replace_call,
                                          task.descriptor.origin_call, DOBBY_HOOK_BACKEND_PLT);
    if (result == DOBBY_ANDROID_OK) {
      task.resolved_target = nullptr;
      task.origin_func = task.descriptor.origin_call ? *task.descriptor.origin_call : nullptr;
      task.status.store(static_cast<int>(TaskStatus::Installed));
      return TaskStatus::Installed;
    }
    return TaskStatus::Failed;
  }

  target = DobbySymbolResolverEx(task.descriptor.image_name.empty() ? nullptr : task.descriptor.image_name.c_str(),
                                 task.descriptor.symbol_name.c_str(), DOBBY_SYMBOL_RESOLVER_DEFAULT, nullptr);
  if (!target)
    return TaskStatus::Pending;

  if (task.descriptor.require_executable_mapping && !is_executable_mapping(target))
    return TaskStatus::Pending;

  result = DobbyHook(target, task.descriptor.replace_call, task.descriptor.origin_call);
  if (result == 0) {
    task.resolved_target = target;
    task.origin_func = task.descriptor.origin_call ? *task.descriptor.origin_call : nullptr;
    task.status.store(static_cast<int>(TaskStatus::Installed));
    return TaskStatus::Installed;
  }

  if (task.descriptor.backend == DOBBY_HOOK_BACKEND_AUTO && !task.descriptor.image_name.empty()) {
    result = DobbyAndroidHookSymbolBackend(task.descriptor.image_name.c_str(), task.descriptor.symbol_name.c_str(),
                                          task.descriptor.replace_call, task.descriptor.origin_call,
                                          DOBBY_HOOK_BACKEND_PLT);
    if (result == DOBBY_ANDROID_OK) {
      task.resolved_target = target;
      task.origin_func = task.descriptor.origin_call ? *task.descriptor.origin_call : nullptr;
      task.status.store(static_cast<int>(TaskStatus::Installed));
      return TaskStatus::Installed;
    }
  }

  return TaskStatus::Failed;
}

void UniversalHookManager::NotifyCallback(Task &task, TaskStatus status) {
  if (!task.descriptor.callback)
    return;
  bool expected = false;
  if (!task.callback_sent.compare_exchange_strong(expected, true))
    return;

  DobbyAutoHookDescriptor callback_descriptor{};
  callback_descriptor.image_name = task.descriptor.image_name.empty() ? nullptr : task.descriptor.image_name.c_str();
  callback_descriptor.symbol_name = task.descriptor.symbol_name.empty() ? nullptr : task.descriptor.symbol_name.c_str();
  callback_descriptor.replace_func = task.descriptor.replace_call;
  callback_descriptor.origin_func = task.descriptor.origin_call;
  callback_descriptor.retry_interval_ms = static_cast<uint32_t>(task.descriptor.retry_interval_ms);
  callback_descriptor.timeout_ms = static_cast<uint32_t>(task.descriptor.timeout_ms);
  callback_descriptor.flags = task.descriptor.flags;
  callback_descriptor.start_delay_ms = static_cast<uint32_t>(task.descriptor.start_delay_ms);
  callback_descriptor.offset = task.descriptor.offset;
  callback_descriptor.target_addr = task.descriptor.target_addr;
  callback_descriptor.backend = task.descriptor.backend;
  callback_descriptor.callback = task.descriptor.callback;
  callback_descriptor.user_data = task.descriptor.user_data;

  task.descriptor.callback(static_cast<int>(status), &callback_descriptor, task.resolved_target, task.origin_func,
                           task.descriptor.user_data);
}

int UniversalHookManager::InstallBlocking(DeferredHookDescriptor descriptor, std::uint64_t timeout_ms) {
  if (!descriptor.replace_call)
    return DOBBY_AUTOHOOK_STATUS_FAILED;
  if (descriptor.symbol_name.empty() && descriptor.offset == 0 && !descriptor.target_addr)
    return DOBBY_AUTOHOOK_STATUS_FAILED;

  Task task;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    task.descriptor = Normalize(descriptor, default_flags_, default_retry_ms_, default_timeout_ms_, 0);
  }
  task.descriptor.start_delay_ms = 0;
  if (timeout_ms != 0)
    task.descriptor.timeout_ms = timeout_ms;
  task.created_at = std::chrono::steady_clock::now();

  while (true) {
    TaskStatus status = TryInstall(task, false);
    if (status == TaskStatus::Installed)
      return 0;
    if (status == TaskStatus::Timeout || status == TaskStatus::Failed || status == TaskStatus::Cancelled)
      return static_cast<int>(status);
    std::this_thread::sleep_for(std::chrono::milliseconds(task.descriptor.retry_interval_ms ? task.descriptor.retry_interval_ms
                                                                                              : kWorkerTickMs));
  }
}

void UniversalHookManager::WorkerLoop() {
  while (running_.load()) {
    std::vector<std::shared_ptr<Task>> snapshot;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      snapshot = tasks_;
    }

    for (auto &task : snapshot) {
      if (!task)
        continue;
      TaskStatus status = TryInstall(*task, false);
      if (status != TaskStatus::Pending)
        NotifyCallback(*task, status);
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [](const std::shared_ptr<Task> &task) {
                     if (!task)
                       return true;
                     return task->status.load() != static_cast<int>(TaskStatus::Pending);
                   }),
                   tasks_.end());
    }

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(kWorkerTickMs), [this] { return !running_.load(); });
  }
}

void UniversalHookManager::Shutdown(bool clear_pending) {
  bool was_running = running_.exchange(false);
  cv_.notify_all();
  if (worker_.joinable())
    worker_.join();

  if (clear_pending) {
    std::vector<std::shared_ptr<Task>> cancelled;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      cancelled.swap(tasks_);
    }
    for (auto &task : cancelled) {
      if (!task)
        continue;
      task->status.store(static_cast<int>(TaskStatus::Cancelled));
      NotifyCallback(*task, TaskStatus::Cancelled);
    }
  }
  (void)was_running;
}

std::size_t UniversalHookManager::PendingCount() {
  std::lock_guard<std::mutex> lock(mutex_);
  return tasks_.size();
}

bool DobbyRegisterAutoHook(const char *image_name,
                           const char *symbol_name,
                           void *replace_call,
                           void **origin_call,
                           std::uint64_t retry_interval_ms) {
  DeferredHookDescriptor descriptor;
  descriptor.image_name = image_name ? image_name : "";
  descriptor.symbol_name = symbol_name ? symbol_name : "";
  descriptor.replace_call = replace_call;
  descriptor.origin_call = origin_call;
  descriptor.retry_interval_ms = retry_interval_ms;
  descriptor.flags = DOBBY_AUTOHOOK_WAIT_MODULE | DOBBY_AUTOHOOK_WAIT_SYMBOL | DOBBY_AUTOHOOK_RETRY |
                     DOBBY_AUTOHOOK_DELAY_FIRST;
  return UniversalHookManager::Shared().Schedule(descriptor);
}

} // namespace dobby

extern "C" PUBLIC int DobbyEnableRuntime(const DobbyRuntimeOptions *options) {
  dobby::UniversalHookManager::Shared().Configure(options);
  return dobby::UniversalHookManager::Shared().Start() ? 0 : DOBBY_AUTOHOOK_STATUS_FAILED;
}

extern "C" PUBLIC int DobbyDisableRuntime(void) {
  dobby::UniversalHookManager::Shared().Shutdown(true);
  return 0;
}

extern "C" PUBLIC int DobbyAutoHook(const DobbyAutoHookDescriptor *descriptor) {
  if (!descriptor || !descriptor->replace_func)
    return DOBBY_AUTOHOOK_STATUS_FAILED;

  dobby::DeferredHookDescriptor runtime_descriptor;
  runtime_descriptor.image_name = descriptor->image_name ? descriptor->image_name : "";
  runtime_descriptor.symbol_name = descriptor->symbol_name ? descriptor->symbol_name : "";
  runtime_descriptor.replace_call = descriptor->replace_func;
  runtime_descriptor.origin_call = descriptor->origin_func;
  runtime_descriptor.retry_interval_ms = descriptor->retry_interval_ms;
  runtime_descriptor.timeout_ms = descriptor->timeout_ms;
  runtime_descriptor.flags = descriptor->flags;
  runtime_descriptor.start_delay_ms = descriptor->start_delay_ms;
  runtime_descriptor.offset = descriptor->offset;
  runtime_descriptor.target_addr = descriptor->target_addr;
  runtime_descriptor.backend = descriptor->backend == 0 ? DOBBY_HOOK_BACKEND_AUTO : descriptor->backend;
  runtime_descriptor.callback = descriptor->callback;
  runtime_descriptor.user_data = descriptor->user_data;

  return dobby::UniversalHookManager::Shared().Schedule(runtime_descriptor) ? 0 : DOBBY_AUTOHOOK_STATUS_FAILED;
}

extern "C" PUBLIC int DobbyAutoHookSymbol(const char *image_name,
                                           const char *symbol_name,
                                           void *replace_func,
                                           void **origin_func) {
  DobbyAutoHookDescriptor descriptor{};
  descriptor.image_name = image_name;
  descriptor.symbol_name = symbol_name;
  descriptor.replace_func = replace_func;
  descriptor.origin_func = origin_func;
  descriptor.retry_interval_ms = 0;
  descriptor.timeout_ms = 0;
  descriptor.flags = DOBBY_AUTOHOOK_WAIT_MODULE | DOBBY_AUTOHOOK_WAIT_SYMBOL | DOBBY_AUTOHOOK_RETRY |
                     DOBBY_AUTOHOOK_DELAY_FIRST;
  descriptor.start_delay_ms = 0;
  descriptor.backend = DOBBY_HOOK_BACKEND_AUTO;
  return DobbyAutoHook(&descriptor);
}

extern "C" PUBLIC int DobbyWaitAndHook(const char *image_name,
                                        const char *symbol_name,
                                        void *replace_func,
                                        void **origin_func,
                                        uint32_t timeout_ms) {
  dobby::DeferredHookDescriptor descriptor;
  descriptor.image_name = image_name ? image_name : "";
  descriptor.symbol_name = symbol_name ? symbol_name : "";
  descriptor.replace_call = replace_func;
  descriptor.origin_call = origin_func;
  descriptor.retry_interval_ms = DOBBY_AUTOHOOK_DEFAULT_RETRY_MS;
  descriptor.timeout_ms = timeout_ms;
  descriptor.start_delay_ms = 0;
  descriptor.flags = DOBBY_AUTOHOOK_WAIT_MODULE | DOBBY_AUTOHOOK_WAIT_SYMBOL | DOBBY_AUTOHOOK_RETRY;
  descriptor.backend = DOBBY_HOOK_BACKEND_AUTO;
  return dobby::UniversalHookManager::Shared().InstallBlocking(descriptor, timeout_ms);
}

extern "C" PUBLIC int DobbyWaitAndHookOffset(const char *image_name,
                                              uintptr_t offset,
                                              void *replace_func,
                                              void **origin_func,
                                              uint32_t timeout_ms) {
  dobby::DeferredHookDescriptor descriptor;
  descriptor.image_name = image_name ? image_name : "";
  descriptor.replace_call = replace_func;
  descriptor.origin_call = origin_func;
  descriptor.retry_interval_ms = DOBBY_AUTOHOOK_DEFAULT_RETRY_MS;
  descriptor.timeout_ms = timeout_ms;
  descriptor.start_delay_ms = 0;
  descriptor.offset = offset;
  descriptor.flags = DOBBY_AUTOHOOK_WAIT_MODULE | DOBBY_AUTOHOOK_OFFSET | DOBBY_AUTOHOOK_RETRY;
  descriptor.backend = DOBBY_HOOK_BACKEND_INLINE;
  return dobby::UniversalHookManager::Shared().InstallBlocking(descriptor, timeout_ms);
}

extern "C" PUBLIC int DobbyAutoHookPendingCount(void) {
  const std::size_t count = dobby::UniversalHookManager::Shared().PendingCount();
  return count > static_cast<std::size_t>(0x7fffffff) ? 0x7fffffff : static_cast<int>(count);
}
