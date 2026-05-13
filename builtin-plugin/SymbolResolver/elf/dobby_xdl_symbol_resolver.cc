#include "dobby_xdl_symbol_resolver.h"

#if defined(__ANDROID__) && defined(DOBBY_ANDROID_USE_XDL)

#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dobby/common.h"
#include "xdl.h"

#ifndef DOBBY_SYMBOL_RESOLVER_DYNSYM_ONLY
#define DOBBY_SYMBOL_RESOLVER_DYNSYM_ONLY 0x1u
#endif
#ifndef DOBBY_SYMBOL_RESOLVER_SYMTAB_ONLY
#define DOBBY_SYMBOL_RESOLVER_SYMTAB_ONLY 0x2u
#endif
#ifndef DOBBY_SYMBOL_RESOLVER_FORCE_LOAD
#define DOBBY_SYMBOL_RESOLVER_FORCE_LOAD 0x4u
#endif
#ifndef DOBBY_SYMBOL_RESOLVER_FULL_PATHNAME
#define DOBBY_SYMBOL_RESOLVER_FULL_PATHNAME 0x8u
#endif

#define DOBBY_XDL_CRASH ((void *)UINTPTR_MAX)

static bool dobby_xdl_ends_with(const char *str, const char *suffix) {
  if (!str || !suffix)
    return false;
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);
  if (suffix_len > str_len)
    return false;
  return strcmp(str + str_len - suffix_len, suffix) == 0;
}

static const char *dobby_xdl_basename(const char *path) {
  if (!path)
    return NULL;
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

static bool dobby_xdl_match_image(const char *loaded_name, const char *image_name, uint32_t flags) {
  if (!image_name || image_name[0] == '\0')
    return true;
  if (!loaded_name || loaded_name[0] == '\0')
    return false;

  if (strcmp(loaded_name, image_name) == 0)
    return true;

  if (flags & DOBBY_SYMBOL_RESOLVER_FULL_PATHNAME)
    return dobby_xdl_ends_with(loaded_name, image_name);

  const char *loaded_base = dobby_xdl_basename(loaded_name);
  const char *image_base = dobby_xdl_basename(image_name);
  return (loaded_base && image_base && strcmp(loaded_base, image_base) == 0) ||
         dobby_xdl_ends_with(loaded_name, image_name);
}

typedef struct {
  const char *lib_name;
  void *dlpi_addr;
  const char *dlpi_name;
  const ElfW(Phdr) * dlpi_phdr;
  size_t dlpi_phnum;
  pthread_mutex_t lock;
} dobby_xdl_handle_cache_t;

#define DOBBY_XDL_CACHE_INIT(name) {name, NULL, NULL, NULL, 0, PTHREAD_MUTEX_INITIALIZER}

static dobby_xdl_handle_cache_t g_xdl_handle_cache[] = {
#if defined(__arm__)
    DOBBY_XDL_CACHE_INIT("linker"),
#elif defined(__aarch64__)
    DOBBY_XDL_CACHE_INIT("linker64"),
#endif
    DOBBY_XDL_CACHE_INIT("libc.so"),
    DOBBY_XDL_CACHE_INIT("libdl.so"),
    DOBBY_XDL_CACHE_INIT("libart.so"),
    DOBBY_XDL_CACHE_INIT("libandroid_runtime.so"),
    DOBBY_XDL_CACHE_INIT("libbinder.so"),
    DOBBY_XDL_CACHE_INIT("libhwui.so"),
    DOBBY_XDL_CACHE_INIT("libandroidfw.so"),
};

static void *dobby_xdl_open_from_cache(dobby_xdl_handle_cache_t *info) {
  if (!info || !__atomic_load_n(&info->dlpi_addr, __ATOMIC_ACQUIRE))
    return NULL;

  struct dl_phdr_info dlinfo;
  memset(&dlinfo, 0, sizeof(dlinfo));
  dlinfo.dlpi_addr = (ElfW(Addr))__atomic_load_n(&info->dlpi_addr, __ATOMIC_RELAXED);
  dlinfo.dlpi_name = __atomic_load_n(&info->dlpi_name, __ATOMIC_RELAXED);
  dlinfo.dlpi_phdr = __atomic_load_n(&info->dlpi_phdr, __ATOMIC_RELAXED);
  dlinfo.dlpi_phnum = (ElfW(Half))__atomic_load_n(&info->dlpi_phnum, __ATOMIC_RELAXED);
  if (!dlinfo.dlpi_addr || !dlinfo.dlpi_phdr || !dlinfo.dlpi_phnum)
    return NULL;

  return xdl_open2(&dlinfo);
}

static void dobby_xdl_save_to_cache(dobby_xdl_handle_cache_t *info, void *handle) {
  if (!info || !handle || handle == DOBBY_XDL_CRASH)
    return;

  xdl_info_t dlinfo;
  memset(&dlinfo, 0, sizeof(dlinfo));
  if (0 != xdl_info(handle, XDL_DI_DLINFO, &dlinfo))
    return;
  if (!dlinfo.dli_fbase || !dlinfo.dlpi_phdr || !dlinfo.dlpi_phnum || !dlinfo.dli_fname)
    return;

  char *name_copy = strdup(dlinfo.dli_fname);
  if (!name_copy)
    return;

  const char *old_name = __atomic_exchange_n(&info->dlpi_name, name_copy, __ATOMIC_ACQ_REL);
  if (old_name)
    free((void *)old_name);
  __atomic_store_n(&info->dlpi_phdr, dlinfo.dlpi_phdr, __ATOMIC_RELEASE);
  __atomic_store_n(&info->dlpi_phnum, dlinfo.dlpi_phnum, __ATOMIC_RELEASE);
  __atomic_store_n(&info->dlpi_addr, dlinfo.dli_fbase, __ATOMIC_RELEASE);
}

static void *dobby_xdl_open_cached_common_library(const char *filename, int xdl_flags) {
  if (!filename)
    return NULL;

  for (size_t i = 0; i < sizeof(g_xdl_handle_cache) / sizeof(g_xdl_handle_cache[0]); i++) {
    dobby_xdl_handle_cache_t *cache = &g_xdl_handle_cache[i];
    if (!dobby_xdl_match_image(filename, cache->lib_name, 0))
      continue;

    void *handle = dobby_xdl_open_from_cache(cache);
    if (handle)
      return handle;

    pthread_mutex_lock(&cache->lock);
    handle = dobby_xdl_open_from_cache(cache);
    if (!handle) {
      handle = xdl_open(filename, xdl_flags);
      if (handle && handle != DOBBY_XDL_CRASH)
        dobby_xdl_save_to_cache(cache, handle);
    }
    pthread_mutex_unlock(&cache->lock);
    return handle;
  }

  return NULL;
}

typedef struct {
  const char *image_name;
  uint32_t flags;
  struct dl_phdr_info dlinfo;
  bool found;
} dobby_xdl_find_image_ctx_t;

static int dobby_xdl_find_image_callback(struct dl_phdr_info *info, size_t size, void *data) {
  (void)size;
  dobby_xdl_find_image_ctx_t *ctx = (dobby_xdl_find_image_ctx_t *)data;
  if (!ctx || !info || !dobby_xdl_match_image(info->dlpi_name, ctx->image_name, ctx->flags))
    return 0;

  memset(&ctx->dlinfo, 0, sizeof(ctx->dlinfo));
  ctx->dlinfo.dlpi_addr = info->dlpi_addr;
  ctx->dlinfo.dlpi_name = info->dlpi_name;
  ctx->dlinfo.dlpi_phdr = info->dlpi_phdr;
  ctx->dlinfo.dlpi_phnum = info->dlpi_phnum;
  ctx->found = true;
  return 1;
}

static int dobby_xdl_open_flags_from_resolver_flags(uint32_t flags) {
  int xdl_flags = XDL_DEFAULT;
  if (flags & DOBBY_SYMBOL_RESOLVER_FORCE_LOAD)
    xdl_flags |= XDL_TRY_FORCE_LOAD;
  return xdl_flags;
}

void *DobbyXdlOpen(const char *filename, int flags) {
  if (!filename || filename[0] == '\0')
    return NULL;

  void *handle = dobby_xdl_open_cached_common_library(filename, flags);
  if (handle)
    return handle;

  handle = xdl_open(filename, flags);
  if (handle)
    return handle;

  dobby_xdl_find_image_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.image_name = filename;
  ctx.flags = 0;
  xdl_iterate_phdr(dobby_xdl_find_image_callback, &ctx, XDL_FULL_PATHNAME);
  if (!ctx.found)
    return NULL;
  return xdl_open2(&ctx.dlinfo);
}

void *DobbyXdlOpenFromInfo(struct dl_phdr_info *info) {
  if (!info)
    return NULL;
  return xdl_open2(info);
}

void *DobbyXdlClose(void *handle) {
  return xdl_close(handle);
}

void *DobbyXdlSym(void *handle, const char *symbol, size_t *symbol_size, uint32_t flags) {
  if (!handle || !symbol || symbol[0] == '\0')
    return NULL;

  void *addr = NULL;
  if (flags & DOBBY_SYMBOL_RESOLVER_DYNSYM_ONLY) {
    addr = xdl_sym(handle, symbol, symbol_size);
  } else if (flags & DOBBY_SYMBOL_RESOLVER_SYMTAB_ONLY) {
    addr = xdl_dsym(handle, symbol, symbol_size);
  } else {
    addr = xdl_sym(handle, symbol, symbol_size);
    if (!addr)
      addr = xdl_dsym(handle, symbol, symbol_size);
  }
  return addr;
}

void *DobbyXdlAddr(void *addr, xdl_info_t *info, void **cache) {
  if (!addr || !info)
    return NULL;
  return 0 == xdl_addr(addr, info, cache) ? info->dli_saddr : NULL;
}

int DobbyXdlInfo(void *handle, xdl_info_t *info) {
  if (!handle || !info)
    return -1;
  return xdl_info(handle, XDL_DI_DLINFO, info);
}

uintptr_t DobbyGetLibraryBase(const char *image_name) {
  void *handle = DobbyXdlOpen(image_name, XDL_DEFAULT);
  if (!handle)
    return 0;
  xdl_info_t info;
  memset(&info, 0, sizeof(info));
  uintptr_t base = 0;
  if (0 == DobbyXdlInfo(handle, &info))
    base = (uintptr_t)info.dli_fbase;
  DobbyXdlClose(handle);
  return base;
}

const char *DobbyGetLibraryPath(void *handle) {
  if (!handle)
    return NULL;
  xdl_info_t info;
  memset(&info, 0, sizeof(info));
  if (0 != DobbyXdlInfo(handle, &info))
    return NULL;
  return info.dli_fname;
}

int DobbyGetAddressInfo(void *addr, xdl_info_t *info) {
  if (!addr || !info)
    return -1;
  return xdl_addr(addr, info, NULL);
}

typedef struct {
  const char *image_name;
  const char *symbol_name;
  uint32_t flags;
  void *result;
  size_t symbol_size;
} dobby_xdl_iterate_symbol_ctx_t;

static int dobby_xdl_iterate_symbol_callback(struct dl_phdr_info *info, size_t size, void *data) {
  (void)size;
  dobby_xdl_iterate_symbol_ctx_t *ctx = (dobby_xdl_iterate_symbol_ctx_t *)data;
  if (!ctx || !info || !ctx->symbol_name)
    return 0;
  if (!dobby_xdl_match_image(info->dlpi_name, ctx->image_name, ctx->flags))
    return 0;

  void *handle = xdl_open2(info);
  if (!handle)
    return 0;

  size_t symbol_size = 0;
  ctx->result = DobbyXdlSym(handle, ctx->symbol_name, &symbol_size, ctx->flags);
  if (ctx->result)
    ctx->symbol_size = symbol_size;
  xdl_close(handle);

  return ctx->result ? 1 : 0;
}

void *dobby_xdl_resolve_symbol_ex(const char *image_name, const char *symbol_name, uint32_t flags,
                                  size_t *symbol_size) {
  if (!symbol_name || symbol_name[0] == '\0')
    return NULL;
  if (symbol_size)
    *symbol_size = 0;

  if (image_name && image_name[0] != '\0') {
    void *handle = DobbyXdlOpen(image_name, dobby_xdl_open_flags_from_resolver_flags(flags));
    if (handle) {
      size_t size = 0;
      void *addr = DobbyXdlSym(handle, symbol_name, &size, flags);
      xdl_close(handle);
      if (addr) {
        if (symbol_size)
          *symbol_size = size;
        return addr;
      }
    }
  }

  dobby_xdl_iterate_symbol_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.image_name = image_name;
  ctx.symbol_name = symbol_name;
  ctx.flags = flags;
  xdl_iterate_phdr(dobby_xdl_iterate_symbol_callback, &ctx, XDL_FULL_PATHNAME);
  if (ctx.result && symbol_size)
    *symbol_size = ctx.symbol_size;
  return ctx.result;
}

void *dobby_xdl_resolve_symbol(const char *image_name, const char *symbol_name) {
  return dobby_xdl_resolve_symbol_ex(image_name, symbol_name, 0, NULL);
}

#else

void *dobby_xdl_resolve_symbol_ex(const char *image_name, const char *symbol_name, uint32_t flags,
                                  size_t *symbol_size) {
  (void)image_name;
  (void)symbol_name;
  (void)flags;
  if (symbol_size)
    *symbol_size = 0;
  return NULL;
}

void *dobby_xdl_resolve_symbol(const char *image_name, const char *symbol_name) {
  (void)image_name;
  (void)symbol_name;
  return NULL;
}

#endif
