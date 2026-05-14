#pragma once

#include "common/linear_allocator.h"
#include "PlatformUnifiedInterface/platform.h"

#include <mutex>

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

struct MemRange {
  addr_t start_;
  size_t size;

  MemRange() : start_(0), size(0) {
  }

  MemRange(addr_t start, size_t size) : start_(start), size(size) {
  }

  addr_t start() const {
    return start_;
  }

  addr_t addr() const {
    return start_;
  }

  addr_t end() const {
    return start_ + size;
  }

  void resize(size_t in_size) {
    size = in_size;
  }

  void reset(addr_t in_start, size_t in_size) {
    start_ = in_start;
    size = in_size;
  }

  bool contains(addr_t in_addr, size_t in_size) const {
    if (in_size == 0) {
      return false;
    }
    if (in_addr < addr()) {
      return false;
    }
    if (in_addr > UINTPTR_MAX - in_size) {
      return false;
    }
    return in_addr + in_size <= end();
  }

  MemRange intersect(const MemRange &other) const {
    auto start = max(this->addr(), other.addr());
    auto end = min(this->end(), other.end());
    if (start < end)
      return MemRange(start, end - start);
    else
      return MemRange{};
  }
};

struct MemBlock : MemRange {
  MemBlock() : MemRange() {
  }

  MemBlock(addr_t start, size_t size) : MemRange(start, size) {
  }
};

using CodeMemBlock = MemBlock;
using DataMemBlock = MemBlock;

struct MemoryAllocator {
  struct PageRecord {
    simple_linear_allocator_t *allocator = nullptr;
    addr_t page = 0;
    bool is_exec = false;
  };

  struct AllocationRecord {
    addr_t addr = 0;
    size_t size = 0;
    addr_t page = 0;
    bool is_exec = false;
    bool is_free = false;
  };

  stl::vector<simple_linear_allocator_t *> code_page_allocators;
  stl::vector<simple_linear_allocator_t *> data_page_allocators;
  stl::vector<PageRecord> page_records;
  stl::vector<AllocationRecord> allocation_records;
  std::mutex mutex_;

  inline static MemoryAllocator *Shared();

  MemBlock allocMemBlock(size_t in_size, bool is_exec = true) {
    std::lock_guard<std::mutex> lock(mutex_);
    return allocMemBlockLocked(in_size, is_exec);
  }

  MemBlock allocExecBlock(size_t size) {
    return allocMemBlock(size, true);
  }

  MemBlock allocDataBlock(size_t size) {
    return allocMemBlock(size, false);
  }

  void freeMemBlock(MemBlock block, bool is_exec = true) {
    std::lock_guard<std::mutex> lock(mutex_);
    freeMemBlockLocked(block, is_exec);
  }

  void freeExecBlock(MemBlock block) {
    freeMemBlock(block, true);
  }

  void freeDataBlock(MemBlock block) {
    freeMemBlock(block, false);
  }

private:
  MemBlock allocMemBlockLocked(size_t in_size, bool is_exec) {
    if (in_size == 0 || in_size > OSMemory::PageSize()) {
      ERROR_LOG("alloc size invalid or too large: %d", in_size);
      return {};
    }

    for (auto &record : allocation_records) {
      if (record.is_free && record.is_exec == is_exec && record.size >= in_size) {
        record.is_free = false;
        return MemBlock(record.addr, in_size);
      }
    }

    uint8_t *result = nullptr;
    auto &allocators = is_exec ? code_page_allocators : data_page_allocators;
    for (auto allocator : allocators) {
      result = (uint8_t *)allocator->alloc((uint32_t)in_size);
      if (result)
        break;
    }

    if (!result) {
      auto page = OSMemory::Allocate(OSMemory::PageSize(), kNoAccess);
      if (!page) {
        ERROR_LOG("allocate memory page failed");
        return {};
      }
      if (!OSMemory::SetPermission(page, OSMemory::PageSize(), is_exec ? kReadExecute : kReadWrite)) {
        OSMemory::Free(page, OSMemory::PageSize());
        return {};
      }
      auto page_allocator = new simple_linear_allocator_t((uint8_t *)page, (uint32_t)OSMemory::PageSize());
      if (is_exec)
        code_page_allocators.push_back(page_allocator);
      else
        data_page_allocators.push_back(page_allocator);

      PageRecord page_record;
      page_record.allocator = page_allocator;
      page_record.page = (addr_t)page;
      page_record.is_exec = is_exec;
      page_records.push_back(page_record);

      result = (uint8_t *)page_allocator->alloc((uint32_t)in_size);
    }

    if (!result) {
      return {};
    }

    AllocationRecord record;
    record.addr = (addr_t)result;
    record.size = in_size;
    record.page = ALIGN_FLOOR((addr_t)result, (uintptr_t)OSMemory::PageSize());
    record.is_exec = is_exec;
    record.is_free = false;
    allocation_records.push_back(record);
    return MemBlock((addr_t)result, in_size);
  }

  void freeMemBlockLocked(MemBlock block, bool is_exec) {
    if (block.addr() == 0 || block.size == 0) {
      return;
    }

    addr_t page = ALIGN_FLOOR(block.addr(), (uintptr_t)OSMemory::PageSize());
    bool found = false;
    for (auto &record : allocation_records) {
      if (record.addr == block.addr() && record.is_exec == is_exec) {
        record.is_free = true;
        found = true;
        break;
      }
    }
    if (!found) {
      DEBUG_LOG("freeMemBlock ignored untracked block: %p", (void *)block.addr());
      return;
    }

    for (auto &record : allocation_records) {
      if (record.page == page && record.is_exec == is_exec && !record.is_free) {
        return;
      }
    }

    simple_linear_allocator_t *page_allocator = nullptr;
    for (auto iter = page_records.begin(); iter != page_records.end(); ++iter) {
      if (iter->page == page && iter->is_exec == is_exec) {
        page_allocator = iter->allocator;
        page_records.erase(iter);
        break;
      }
    }

    auto &allocators = is_exec ? code_page_allocators : data_page_allocators;
    if (page_allocator) {
      for (auto iter = allocators.begin(); iter != allocators.end(); ++iter) {
        if (*iter == page_allocator) {
          allocators.erase(iter);
          break;
        }
      }
    }

    for (auto iter = allocation_records.begin(); iter != allocation_records.end();) {
      if (iter->page == page && iter->is_exec == is_exec) {
        iter = allocation_records.erase(iter);
      } else {
        ++iter;
      }
    }

    if (page_allocator) {
      delete page_allocator;
      OSMemory::Free((void *)page, OSMemory::PageSize());
    }
  }
};

inline static MemoryAllocator gMemoryAllocator;
MemoryAllocator *MemoryAllocator::Shared() {
  return &gMemoryAllocator;
}

#undef min
#undef max
