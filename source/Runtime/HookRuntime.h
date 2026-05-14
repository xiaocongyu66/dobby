#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace dobby {

enum class HookBackend {
  Inline = 0,
  NearInline = 1,
  PLT = 2,
  GOT = 3,
  VTable = 4,
  Auto = 5,
};

enum class HookState {
  Created = 0,
  Prepared = 1,
  Patched = 2,
  Enabled = 3,
  Disabled = 4,
  Destroyed = 5,
  Failed = 6,
};

struct HookRecord {
  void *target = nullptr;
  void *replace = nullptr;
  void *backup = nullptr;
  void *trampoline = nullptr;
  HookBackend backend = HookBackend::Auto;
  HookState state = HookState::Created;
  std::string image;
  std::string symbol;
  std::uintptr_t offset = 0;
  std::uintptr_t patch_addr = 0;
  std::size_t patch_size = 0;
  std::uint64_t transaction_id = 0;
  bool enabled = false;
};

class HookRuntime {
public:
  static HookRuntime &Shared();

  std::uint64_t BeginTransaction();
  bool CommitTransaction();
  bool RollbackTransaction();
  bool RollbackTransaction(std::uint64_t transaction_id);

  bool Register(HookRecord record);
  bool Upsert(HookRecord record);
  bool Remove(void *target);
  bool RemoveByTransaction(std::uint64_t transaction_id);
  bool Find(void *target, HookRecord *out_record);
  bool FindByTransaction(std::uint64_t transaction_id, std::vector<HookRecord> *out_records);
  bool UpdateState(void *target, HookState state);
  bool UpdateEnabled(void *target, bool enabled);
  std::vector<HookRecord> Snapshot();
  std::vector<HookRecord> Snapshot(std::uint64_t transaction_id);
  void Clear();

  std::size_t Count();

private:
  HookRuntime() = default;

  std::mutex mutex_;
  std::vector<HookRecord> records_;
  std::uint64_t next_transaction_id_ = 1;
  std::uint64_t active_transaction_id_ = 0;
  int transaction_depth_ = 0;
};

} // namespace dobby
