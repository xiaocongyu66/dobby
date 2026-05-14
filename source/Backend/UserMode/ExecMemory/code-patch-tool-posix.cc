#include "dobby/dobby_internal.h"
#include "core/arch/CpuRegister.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#if !defined(__APPLE__)

#if defined(__ANDROID__) || defined(__linux__)
namespace {

int page_protection_from_maps(uintptr_t page, int *out_prot) {
  if (!out_prot) {
    return -1;
  }

  FILE *fp = fopen("/proc/self/maps", "r");
  if (!fp) {
    ERROR_LOG("DobbyCodePatch failed to open /proc/self/maps: %s", strerror(errno));
    return -1;
  }

  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    unsigned long long start = 0;
    unsigned long long end = 0;
    char perm[5] = {0};
    if (sscanf(line, "%llx-%llx %4s", &start, &end, perm) != 3) {
      continue;
    }
    if (page >= (uintptr_t)start && page < (uintptr_t)end) {
      int prot = PROT_NONE;
      if (perm[0] == 'r') {
        prot |= PROT_READ;
      }
      if (perm[1] == 'w') {
        prot |= PROT_WRITE;
      }
      if (perm[2] == 'x') {
        prot |= PROT_EXEC;
      }
      fclose(fp);
      *out_prot = prot;
      return 0;
    }
  }

  fclose(fp);
  ERROR_LOG("DobbyCodePatch failed to locate page in /proc/self/maps: page=%p", (void *)page);
  return -1;
}

void restore_page_permissions(const std::vector<uintptr_t> &pages, const std::vector<int> &protections) {
  for (size_t i = 0; i < pages.size() && i < protections.size(); ++i) {
    if (mprotect((void *)pages[i], (size_t)sysconf(_SC_PAGESIZE), protections[i]) != 0) {
      ERROR_LOG("DobbyCodePatch failed to restore page permission: page=%p error=%s", (void *)pages[i], strerror(errno));
    }
  }
}

} // namespace
#endif

PUBLIC int DobbyCodePatch(void *address, uint8_t *buffer, uint32_t buffer_size) {
#if defined(__ANDROID__) || defined(__linux__)
  if (!address || !buffer || buffer_size == 0) {
    ERROR_LOG("DobbyCodePatch invalid argument: address=%p buffer=%p size=%u", address, buffer, buffer_size);
    return -1;
  }

  long page_size_long = sysconf(_SC_PAGESIZE);
  if (page_size_long <= 0) {
    ERROR_LOG("DobbyCodePatch failed to query page size");
    return -1;
  }

  const uintptr_t page_size = (uintptr_t)page_size_long;
  const uintptr_t patch_start = (uintptr_t)address;
  const uintptr_t patch_end = patch_start + (uintptr_t)buffer_size - 1;
  if (patch_end < patch_start) {
    ERROR_LOG("DobbyCodePatch address range overflow: address=%p size=%u", address, buffer_size);
    return -1;
  }

  const uintptr_t first_page = ALIGN_FLOOR(patch_start, page_size);
  const uintptr_t last_page = ALIGN_FLOOR(patch_end, page_size);

  std::vector<uintptr_t> pages;
  std::vector<int> original_protections;
  for (uintptr_t page = first_page;; page += page_size) {
    int prot = PROT_NONE;
    if (page_protection_from_maps(page, &prot) != 0) {
      return -1;
    }
    pages.push_back(page);
    original_protections.push_back(prot);
    if (page == last_page) {
      break;
    }
    if (page > UINTPTR_MAX - page_size) {
      ERROR_LOG("DobbyCodePatch page range overflow: address=%p size=%u", address, buffer_size);
      return -1;
    }
  }

  std::vector<uint8_t> original_bytes(buffer_size);
  memcpy(original_bytes.data(), address, buffer_size);

  size_t writable_pages = 0;
  for (size_t i = 0; i < pages.size(); ++i) {
    const int write_prot = original_protections[i] | PROT_WRITE;
    if (mprotect((void *)pages[i], page_size, write_prot) != 0) {
      ERROR_LOG("DobbyCodePatch failed to make page writable: page=%p error=%s", (void *)pages[i], strerror(errno));
      restore_page_permissions(pages, original_protections);
      return -1;
    }
    ++writable_pages;
  }

  memcpy(address, buffer, buffer_size);
  addr_t clear_start = (addr_t)address;
  ClearCache((void *)clear_start, (void *)(clear_start + buffer_size));

  int restore_error = 0;
  for (size_t i = 0; i < pages.size(); ++i) {
    if (mprotect((void *)pages[i], page_size, original_protections[i]) != 0) {
      ERROR_LOG("DobbyCodePatch failed to restore page permission: page=%p error=%s", (void *)pages[i], strerror(errno));
      restore_error = -1;
    }
  }

  if (restore_error != 0) {
    for (size_t i = 0; i < writable_pages; ++i) {
      (void)mprotect((void *)pages[i], page_size, original_protections[i] | PROT_WRITE);
    }
    memcpy(address, original_bytes.data(), buffer_size);
    ClearCache((void *)clear_start, (void *)(clear_start + buffer_size));
    restore_page_permissions(pages, original_protections);
    return -1;
  }

  return 0;
#else
  (void)address;
  (void)buffer;
  (void)buffer_size;
  return -1;
#endif
}

#endif
