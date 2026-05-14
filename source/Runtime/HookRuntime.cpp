#include "HookRuntime.h"

#include <algorithm>
#include <utility>

namespace dobby {

HookRuntime &HookRuntime::Shared() {
  static HookRuntime runtime;
  return runtime;
}

std::uint64_t HookRuntime::BeginTransaction() {
  std::lock_guard<std::mutex> lock(mutex_);
  ++transaction_depth_;
  if (transaction_depth_ == 1) {
    active_transaction_id_ = next_transaction_id_++;
  }
  return active_transaction_id_;
}

bool HookRuntime::CommitTransaction() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (transaction_depth_ <= 0) {
    return false;
  }
  --transaction_depth_;
  if (transaction_depth_ == 0) {
    active_transaction_id_ = 0;
  }
  return true;
}

bool HookRuntime::RollbackTransaction(std::uint64_t transaction_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto old_size = records_.size();
  records_.erase(
      std::remove_if(records_.begin(), records_.end(),
                     [transaction_id](const HookRecord &record) { return record.transaction_id == transaction_id; }),
      records_.end());
  if (transaction_depth_ > 0 && active_transaction_id_ == transaction_id) {
    transaction_depth_ = 0;
    active_transaction_id_ = 0;
  }
  return records_.size() != old_size;
}

bool HookRuntime::RollbackTransaction() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (transaction_depth_ <= 0) {
    return false;
  }

  const std::uint64_t tx_id = active_transaction_id_;
  records_.erase(
      std::remove_if(records_.begin(), records_.end(),
                     [tx_id](const HookRecord &record) { return record.transaction_id == tx_id; }),
      records_.end());
  transaction_depth_ = 0;
  active_transaction_id_ = 0;
  return true;
}

bool HookRuntime::Register(HookRecord record) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (record.transaction_id == 0 && transaction_depth_ > 0) {
    record.transaction_id = active_transaction_id_;
  }
  auto it = std::find_if(records_.begin(), records_.end(),
                         [&record](const HookRecord &item) { return item.target == record.target; });
  if (it == records_.end()) {
    records_.push_back(record);
  } else {
    *it = record;
  }
  return true;
}

bool HookRuntime::Upsert(HookRecord record) {
  return Register(std::move(record));
}

bool HookRuntime::Remove(void *target) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(records_.begin(), records_.end(),
                         [target](const HookRecord &record) { return record.target == target; });
  if (it == records_.end()) {
    return false;
  }
  records_.erase(it);
  return true;
}

bool HookRuntime::RemoveByTransaction(std::uint64_t transaction_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto old_size = records_.size();
  records_.erase(
      std::remove_if(records_.begin(), records_.end(),
                     [transaction_id](const HookRecord &record) { return record.transaction_id == transaction_id; }),
      records_.end());
  return records_.size() != old_size;
}

bool HookRuntime::Find(void *target, HookRecord *out_record) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(records_.begin(), records_.end(),
                         [target](const HookRecord &record) { return record.target == target; });
  if (it == records_.end()) {
    return false;
  }
  if (out_record) {
    *out_record = *it;
  }
  return true;
}

bool HookRuntime::FindByTransaction(std::uint64_t transaction_id, std::vector<HookRecord> *out_records) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!out_records) {
    return false;
  }
  out_records->clear();
  for (const auto &record : records_) {
    if (record.transaction_id == transaction_id) {
      out_records->push_back(record);
    }
  }
  return !out_records->empty();
}

bool HookRuntime::UpdateState(void *target, HookState state) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(records_.begin(), records_.end(),
                         [target](const HookRecord &record) { return record.target == target; });
  if (it == records_.end()) {
    return false;
  }
  it->state = state;
  return true;
}

bool HookRuntime::UpdateEnabled(void *target, bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(records_.begin(), records_.end(),
                         [target](const HookRecord &record) { return record.target == target; });
  if (it == records_.end()) {
    return false;
  }
  it->enabled = enabled;
  it->state = enabled ? HookState::Enabled : HookState::Disabled;
  return true;
}

std::vector<HookRecord> HookRuntime::Snapshot() {
  std::lock_guard<std::mutex> lock(mutex_);
  return records_;
}

std::vector<HookRecord> HookRuntime::Snapshot(std::uint64_t transaction_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<HookRecord> out;
  for (const auto &record : records_) {
    if (record.transaction_id == transaction_id) {
      out.push_back(record);
    }
  }
  return out;
}

void HookRuntime::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  records_.clear();
  transaction_depth_ = 0;
  active_transaction_id_ = 0;
}

std::size_t HookRuntime::Count() {
  std::lock_guard<std::mutex> lock(mutex_);
  return records_.size();
}

} // namespace dobby
