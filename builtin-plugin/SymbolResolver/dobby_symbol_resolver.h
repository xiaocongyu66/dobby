#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(BUILDING_INTERNAL)
#include "macho/dobby_symbol_resolver_priv.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void *DobbySymbolResolver(const char *image_name, const char *symbol_name);
void *DobbySymbolResolverEx(const char *image_name, const char *symbol_name, uint32_t flags,
                            size_t *symbol_size);

#ifdef __cplusplus
}
#endif
