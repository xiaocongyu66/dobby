#include "dobby/common.h"
#include "SymbolResolver/dobby_symbol_resolver.h"

extern "C" PUBLIC int DobbyHookBySymbolEx(const char *image_name, const char *symbol_name, void *fake_func,
                                          void **out_origin_func, uint32_t flags, size_t *symbol_size) {
  if (!symbol_name || !fake_func) {
    ERROR_LOG("invalid hook args: image=%s symbol=%s fake=%p", image_name ? image_name : "<all>",
              symbol_name ? symbol_name : "<null>", fake_func);
    return -1;
  }

  size_t resolved_size = 0;
  void *address = DobbySymbolResolverEx(image_name, symbol_name, flags, &resolved_size);
  if (symbol_size)
    *symbol_size = resolved_size;
  if (!address) {
    ERROR_LOG("symbol not found: image=%s symbol=%s", image_name ? image_name : "<all>", symbol_name);
    return -2;
  }

  int result = DobbyHook(address, fake_func, out_origin_func);
#if defined(__ANDROID__)
  if (result != 0 && image_name) {
    // Fall back to PLT/GOT hook when the function body is too short or relocated code
    // cannot be rewritten safely. This keeps the target reachable through imports.
    extern int DobbyAndroidHookPLT(const char *image_name, const char *symbol_name, void *replace, void **origin);
    result = DobbyAndroidHookPLT(image_name, symbol_name, fake_func, out_origin_func);
    if (result == 0)
      return 0;
  }
#endif
  return result;
}

extern "C" PUBLIC int DobbyHookBySymbol(const char *image_name, const char *symbol_name, void *fake_func,
                                        void **out_origin_func) {
  return DobbyHookBySymbolEx(image_name, symbol_name, fake_func, out_origin_func, 0, NULL);
}

extern "C" PUBLIC int DobbyHookBySymbolCallback(const char *image_name, const char *symbol_name, void *fake_func,
                                                void **out_origin_func, uint32_t flags,
                                                dobby_hooked_callback_t hooked, void *hooked_arg) {
  size_t symbol_size = 0;
  void *address = DobbySymbolResolverEx(image_name, symbol_name, flags, &symbol_size);
  int result = -2;
  if (address)
    result = DobbyHook(address, fake_func, out_origin_func);

  if (hooked) {
    void *origin = out_origin_func ? *out_origin_func : NULL;
    hooked(result, image_name, symbol_name, address, fake_func, origin, hooked_arg);
  }
  return result;
}

extern "C" PUBLIC int DobbyDestroyBySymbol(const char *image_name, const char *symbol_name) {
  void *address = DobbySymbolResolverEx(image_name, symbol_name, 0, NULL);
  if (!address)
    return -2;
  return DobbyDestroy(address);
}
