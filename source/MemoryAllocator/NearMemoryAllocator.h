#pragma once

#include "dobby/common.h"
#include "MemoryAllocator.h"
#include "PlatformUtil/ProcessRuntime.h"
#include <stdint.h>
#include <mutex>

#define KB (1024uLL)
#define MB (1024uLL * KB)
#define GB (1024uLL * MB)

// memmem impl
inline void *memmem_impl(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen) {
  if (!haystack || !needle) {
    return (void *)haystack;
  } else {
    const char *h = (const char *)haystack;
    const char *n = (const char *)needle;
    size_t l = needlelen;
    const char *r = h;
    while (l && (l <= haystacklen)) {
      if (*n++ != *h++) {
        r = h;
        n = (const char *)needle;
        l = needlelen;
      } else {
        --l;
      }
      --haystacklen;
    }
    return l ? nullptr : (void *)r;
  }
}

inline dobby_alloc_near_code_callback_t custom_alloc_near_code_handler = nullptr;
PUBLIC inline void dobby_register_alloc_near_code_callback(dobby_alloc_near_code_callback_t handler) {
  features::apple::arm64e_pac_strip_and_sign(handler);
  custom_alloc_near_code_handler = handler;
}

struct NearMemoryAllocator {
  struct PageRecord {
    simple_linear_allocator_t *allocator = nullptr;
    addr_t page = 0;
    bool is_exec = false;
    bool owned_page = true;
  };

  struct AllocationRecord {
    addr_t addr = 0;
    size_t size = 0;
    addr_t page = 0;
    bool is_exec = false;
    bool is_free = false;
    bool owned_page = true;
  };

  stl::vector<simple_linear_allocator_t*> code_page_allocators;
  stl::vector<simple_linear_allocator_t*> data_page_allocators;
  stl::vector<PageRecord> page_records;
  stl::vector<AllocationRecord> allocation_records;
  std::mutex mutex_;

  inline static NearMemoryAllocator *Shared();

  MemBlock allocNearCodeBlock(uint32_t in_size, addr_t pos, size_t range) {
    if (in_size == 0 || pos < range || range > (SIZE_MAX / 2)) {
      return {};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (custom_alloc_near_code_handler) {
      auto addr = custom_alloc_near_code_handler(in_size, pos, range);
      if (addr) {
        AllocationRecord record;
        record.addr = addr;
        record.size = in_size;
        record.page = ALIGN_FLOOR(addr, (uintptr_t)OSMemory::PageSize());
        record.is_exec = true;
        record.is_free = false;
        record.owned_page = false;
        allocation_records.push_back(record);
        return {addr, in_size};
      }
    } else {
      auto search_range = MemRange(pos - range, range * 2);
      return allocNearBlockLocked(in_size, search_range, true);
    }
    return {};
  }

  MemBlock allocNearDataBlock(uint32_t in_size, addr_t pos, size_t range) {
    if (in_size == 0 || pos < range || range > (SIZE_MAX / 2)) {
      return {};
    }
    auto search_range = MemRange(pos - range, range * 2);
    std::lock_guard<std::mutex> lock(mutex_);
    return allocNearBlockLocked(in_size, search_range, false);
  }

  void freeNearCodeBlock(MemBlock block) {
    std::lock_guard<std::mutex> lock(mutex_);
    freeNearBlockLocked(block, true);
  }

  void freeNearDataBlock(MemBlock block) {
    std::lock_guard<std::mutex> lock(mutex_);
    freeNearBlockLocked(block, false);
  }

private:
  MemBlock allocNearBlockLocked(uint32_t in_size, MemRange search_range, bool is_exec = true) {
    for (auto &record : allocation_records) {
      if (record.is_free && record.is_exec == is_exec && record.size >= in_size &&
          search_range.contains(record.addr, in_size)) {
        record.is_free = false;
        return {(addr_t)record.addr, (size_t)in_size};
      }
    }

    // step-1: search from allocators first
    auto &allocators = is_exec ? code_page_allocators : data_page_allocators;
    for (auto allocator : allocators) {
      auto cursor = allocator->cursor();
      auto unused_size = allocator->capacity - allocator->size;
      auto unused_range = MemRange((addr_t)cursor, unused_size);
      auto intersect = search_range.intersect(unused_range);
      if (intersect.size < in_size)
        continue;

      auto gap_size = intersect.addr() - (addr_t)cursor;
      if (gap_size) {
        allocator->alloc(gap_size);
      }

      auto result = allocator->alloc(in_size);
      DEBUG_LOG("step-1 allocator: %p, size: %d", (void *)result, in_size);
      if (!result) {
        continue;
      }
      AllocationRecord record;
      record.addr = (addr_t)result;
      record.size = in_size;
      record.page = ALIGN_FLOOR((addr_t)result, (uintptr_t)OSMemory::PageSize());
      record.is_exec = is_exec;
      record.is_free = false;
      record.owned_page = true;
      allocation_records.push_back(record);
      return {(addr_t)result, (size_t)in_size};
    }

    // step-2: search from unused page between regions
    auto regions = ProcessRuntime::getMemoryLayout();
    for (int i = 0; i < regions.size(); ++i) {
      auto *region = &regions[i];
      auto *prev_region = i > 0 ? &regions[i - 1] : nullptr;
      auto *next_region = i < regions.size() - 1 ? &regions[i + 1] : nullptr;
      if (!next_region)
        break;

      auto unused_region_start = region->end();
      auto unused_region_size = next_region->addr() - region->end();
      MemRegion unused_region(unused_region_start, unused_region_size, kNoAccess);
      auto intersect = search_range.intersect(unused_region);
      if (intersect.size < in_size)
        continue;

      auto unused_page = (void *)ALIGN_FLOOR(intersect.addr(), OSMemory::PageSize());
      {
        auto page = OSMemory::Allocate(OSMemory::PageSize(), kNoAccess, unused_page);
        if (page != unused_page) {
          FATAL_LOG("allocate unused page failed");
        }
        OSMemory::SetPermission(unused_page, OSMemory::PageSize(), is_exec ? kReadExecute : kReadWrite);
        DEBUG_LOG("step-2 unused page: %p", unused_page);
        auto page_allocator = new simple_linear_allocator_t((uint8_t *)unused_page, OSMemory::PageSize());
        if (is_exec)
          code_page_allocators.push_back(page_allocator);
        else
          data_page_allocators.push_back(page_allocator);
        PageRecord page_record;
        page_record.allocator = page_allocator;
        page_record.page = (addr_t)unused_page;
        page_record.is_exec = is_exec;
        page_record.owned_page = true;
        page_records.push_back(page_record);
      }
      // should be fallthrough to step-1 allocator
      return allocNearBlockLocked(in_size, search_range, is_exec);
    }

    // step-3 for exec only
    if (!is_exec) {
      return {};
    }

    // step-3: search unused code gap in regions
    const uint8_t invalid_code_seq[0x1000] = {0};
    for (int i = 0; i < regions.size(); ++i) {
      auto *region = &regions[i];
      if (!(region->perm & MEM_PERM_X))
        continue;

      auto intersect = search_range.intersect(*region);
      if (intersect.size < in_size)
        continue;

      auto search_start = intersect.addr();
      auto search_size = intersect.size;

      auto alignmemt = 4;
      auto unused_code_gap =
          memmem_impl((void *)search_start, search_size, invalid_code_seq, in_size + (alignmemt - 1));
      if (!unused_code_gap)
        continue;
      unused_code_gap = (void *)ALIGN_CEIL(unused_code_gap, alignmemt);
      DEBUG_LOG("step-3 unused code gap: %p, size: %d", unused_code_gap, in_size);
      AllocationRecord record;
      record.addr = (addr_t)unused_code_gap;
      record.size = in_size;
      record.page = ALIGN_FLOOR((addr_t)unused_code_gap, (uintptr_t)OSMemory::PageSize());
      record.is_exec = is_exec;
      record.is_free = false;
      record.owned_page = false;
      allocation_records.push_back(record);
      return {(addr_t)unused_code_gap, (size_t)in_size};
    }

    return {};
  }

  void freeNearBlockLocked(MemBlock block, bool is_exec) {
    if (block.addr() == 0 || block.size == 0) {
      return;
    }

    addr_t page = ALIGN_FLOOR(block.addr(), (uintptr_t)OSMemory::PageSize());
    bool found = false;
    bool owned_page = false;
    for (auto &record : allocation_records) {
      if (record.addr == block.addr() && record.is_exec == is_exec) {
        record.is_free = true;
        owned_page = record.owned_page;
        found = true;
        break;
      }
    }
    if (!found || !owned_page) {
      return;
    }

    for (auto &record : allocation_records) {
      if (record.page == page && record.is_exec == is_exec && !record.is_free) {
        return;
      }
    }

    simple_linear_allocator_t *page_allocator = nullptr;
    for (auto iter = page_records.begin(); iter != page_records.end(); ++iter) {
      if (iter->page == page && iter->is_exec == is_exec && iter->owned_page) {
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
      if (iter->page == page && iter->is_exec == is_exec && iter->owned_page) {
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

inline static NearMemoryAllocator gNearMemoryAllocator;
NearMemoryAllocator *NearMemoryAllocator::Shared() {
  return &gNearMemoryAllocator;
}