#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __ANDROID__
#include <link.h>
#include "xdl.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void *dobby_xdl_resolve_symbol_ex(const char *image_name, const char *symbol_name, uint32_t flags,
                                  size_t *symbol_size);
void *dobby_xdl_resolve_symbol(const char *image_name, const char *symbol_name);

#ifdef __ANDROID__
void *DobbyXdlOpen(const char *filename, int flags);
void *DobbyXdlOpenFromInfo(struct dl_phdr_info *info);
void *DobbyXdlClose(void *handle);
void *DobbyXdlSym(void *handle, const char *symbol, size_t *symbol_size, uint32_t flags);
void *DobbyXdlAddr(void *addr, xdl_info_t *info, void **cache);
int DobbyXdlInfo(void *handle, xdl_info_t *info);
uintptr_t DobbyGetLibraryBase(const char *image_name);
const char *DobbyGetLibraryPath(void *handle);
int DobbyGetAddressInfo(void *addr, xdl_info_t *info);
#endif

#ifdef __cplusplus
}
#endif
