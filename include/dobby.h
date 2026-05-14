#ifndef dobby_h
#define dobby_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

typedef uintptr_t addr_t;
typedef uint32_t addr32_t;
typedef uint64_t addr64_t;

typedef void *asm_func_t;

#if defined(__arm__)
typedef struct {
  uint32_t dummy_0;
  uint32_t dummy_1;

  uint32_t dummy_2;
  uint32_t sp;

  union {
    uint32_t r[13];
    struct {
      uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12;
    } regs;
  } general;

  uint32_t lr;
} DobbyRegisterContext;
#elif defined(__arm64__) || defined(__aarch64__)
#define ARM64_TMP_REG_NDX_0 17

typedef union _FPReg {
  __int128_t q;
  struct {
    double d1;
    double d2;
  } d;
  struct {
    float f1;
    float f2;
    float f3;
    float f4;
  } f;
} FPReg;

// register context
typedef struct {
  uint64_t dmmpy_0; // dummy placeholder
  uint64_t sp;

  uint64_t dmmpy_1; // dummy placeholder
  union {
    uint64_t x[29];
    struct {
      uint64_t x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17, x18, x19, x20, x21, x22,
          x23, x24, x25, x26, x27, x28;
    } regs;
  } general;

  uint64_t fp;
  uint64_t lr;

  union {
    FPReg q[32];
    struct {
      FPReg q0, q1, q2, q3, q4, q5, q6, q7;
      // [!!! READ ME !!!]
      // for Arm64, can't access q8 - q31, unless you enable full floating-point register pack
      FPReg q8, q9, q10, q11, q12, q13, q14, q15, q16, q17, q18, q19, q20, q21, q22, q23, q24, q25, q26, q27, q28, q29,
          q30, q31;
    } regs;
  } floating;
} DobbyRegisterContext;
#elif defined(_M_IX86) || defined(__i386__)
typedef struct _RegisterContext {
  uint32_t dummy_0;
  uint32_t esp;

  uint32_t dummy_1;
  uint32_t flags;

  union {
    struct {
      uint32_t eax, ebx, ecx, edx, ebp, esp, edi, esi;
    } regs;
  } general;

} DobbyRegisterContext;
#elif defined(_M_X64) || defined(__x86_64__)
typedef struct {
  union {
    struct {
      uint64_t rax, rbx, rcx, rdx, rbp, rsp, rdi, rsi, r8, r9, r10, r11, r12, r13, r14, r15;
    } regs;
  } general;

  uint64_t dummy_0;
  uint64_t flags;
  uint64_t ret;
} DobbyRegisterContext;
#endif

#define install_hook_name(name, fn_ret_t, fn_args_t...)                                                                \
  static fn_ret_t fake_##name(fn_args_t);                                                                              \
  static fn_ret_t (*orig_##name)(fn_args_t);                                                                           \
  /* __attribute__((constructor)) */ static void install_hook_##name(void *sym_addr) {                                 \
    DobbyHook(sym_addr, (void *)fake_##name, (void **)&orig_##name);                                                   \
    return;                                                                                                            \
  }                                                                                                                    \
  fn_ret_t fake_##name(fn_args_t)

// memory code patch
int DobbyCodePatch(void *address, uint8_t *buffer, uint32_t buffer_size);

// function inline hook
int DobbyHook(void *address, void *fake_func, void **out_origin_func);

// dynamic binary instruction instrument
// for Arm64, can't access q8 - q31, unless enable full floating-point register pack
typedef void (*dobby_instrument_callback_t)(void *address, DobbyRegisterContext *ctx);
int DobbyInstrument(void *address, dobby_instrument_callback_t pre_handler);

// destroy and restore code patch
int DobbyDestroy(void *address);

const char *DobbyGetVersion();

// symbol resolver
void *DobbySymbolResolver(const char *image_name, const char *symbol_name);

// Extended symbol resolving flags. On Android builds with DOBBY_ANDROID_USE_XDL,
// these control bundled xDL lookup. Default lookup checks .dynsym first and then
// .symtab, matching the behavior commonly used by Android inline-hook libraries.
#define DOBBY_SYMBOL_RESOLVER_DEFAULT 0u
#define DOBBY_SYMBOL_RESOLVER_DYNSYM_ONLY (1u << 0)
#define DOBBY_SYMBOL_RESOLVER_SYMTAB_ONLY (1u << 1)
#define DOBBY_SYMBOL_RESOLVER_FORCE_LOAD (1u << 2)
#define DOBBY_SYMBOL_RESOLVER_FULL_PATHNAME (1u << 3)

void *DobbySymbolResolverEx(const char *image_name, const char *symbol_name, uint32_t flags,
                            size_t *symbol_size);

// Resolve a symbol and hook it in one step.
int DobbyHookBySymbol(const char *image_name, const char *symbol_name, void *fake_func, void **out_origin_func);
int DobbyHookBySymbolEx(const char *image_name, const char *symbol_name, void *fake_func,
                        void **out_origin_func, uint32_t flags, size_t *symbol_size);
typedef void (*dobby_hooked_callback_t)(int error_number, const char *image_name, const char *symbol_name,
                                        void *symbol_addr, void *fake_func, void *origin_func, void *arg);
int DobbyHookBySymbolCallback(const char *image_name, const char *symbol_name, void *fake_func,
                              void **out_origin_func, uint32_t flags, dobby_hooked_callback_t hooked,
                              void *hooked_arg);
int DobbyDestroyBySymbol(const char *image_name, const char *symbol_name);

// trampoline / hook allocation tuning
void dobby_set_near_trampoline(bool enable);
typedef addr_t (*dobby_alloc_near_code_callback_t)(uint32_t size, addr_t pos, size_t range);
void dobby_register_alloc_near_code_callback(dobby_alloc_near_code_callback_t handler);
void dobby_set_options(bool enable_near_trampoline, dobby_alloc_near_code_callback_t alloc_near_code_callback);

#if defined(__linux__) || defined(__ANDROID__)
// Bundled official xDL 2.3.0 API. These symbols are built into Dobby when
// DOBBY_ANDROID_USE_XDL=ON, so users can include only dobby.h and link/package
// only libdobby.
#ifndef IO_GITHUB_HEXHACKING_XDL
#define IO_GITHUB_HEXHACKING_XDL
#include <dlfcn.h>
#include <link.h>

typedef struct {
  const char *dli_fname;
  void *dli_fbase;
  const char *dli_sname;
  void *dli_saddr;
  size_t dli_ssize;
  const ElfW(Phdr) *dlpi_phdr;
  size_t dlpi_phnum;
} xdl_info_t;

#define XDL_DEFAULT 0x00
#define XDL_TRY_FORCE_LOAD 0x01
#define XDL_ALWAYS_FORCE_LOAD 0x02
void *xdl_open(const char *filename, int flags);
void *xdl_open2(struct dl_phdr_info *info);
void *xdl_close(void *handle);
void *xdl_sym(void *handle, const char *symbol, size_t *symbol_size);
void *xdl_dsym(void *handle, const char *symbol, size_t *symbol_size);

#define XDL_NON_SYM 0x01
int xdl_addr(void *addr, xdl_info_t *info, void **cache);
int xdl_addr4(void *addr, xdl_info_t *info, void **cache, int flags);
void xdl_addr_clean(void **cache);

#define XDL_FULL_PATHNAME 0x01
int xdl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *), void *data, int flags);

#define XDL_DI_DLINFO 1
int xdl_info(void *handle, int request, void *info);
#endif

// Dobby-prefixed xDL helpers. They add cached open for common Android system
// libraries and a single symbol helper which can select .dynsym or .symtab by
// using DOBBY_SYMBOL_RESOLVER_* flags.
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



// Hook extension APIs - internalized hook backends and management.
// These APIs focus on real hook modes: inline, PLT/GOT, vtable, symbol-based
// smart fallback, and hook bookkeeping.

#if defined(__linux__) || defined(__ANDROID__)

#define DOBBY_ANDROID_NAME_MAX 256

typedef enum {
  DOBBY_ANDROID_OK = 0,
  DOBBY_ANDROID_ERR_INVALID_ARGUMENT = -1,
  DOBBY_ANDROID_ERR_LIBRARY_NOT_FOUND = -2,
  DOBBY_ANDROID_ERR_SYMBOL_NOT_FOUND = -3,
  DOBBY_ANDROID_ERR_ALREADY_HOOKED = -4,
  DOBBY_ANDROID_ERR_HOOK_FAILED = -5,
  DOBBY_ANDROID_ERR_UNHOOK_FAILED = -6,
  DOBBY_ANDROID_ERR_BUFFER_TOO_SMALL = -7,
} DobbyAndroidStatus;

typedef enum {
  DOBBY_ANDROID_HOOK_BACKEND_INLINE = 0,
  DOBBY_ANDROID_HOOK_BACKEND_PLT = 1,
  DOBBY_ANDROID_HOOK_BACKEND_VTABLE = 2,
} DobbyAndroidHookBackend;

typedef struct {
  char image_name[DOBBY_ANDROID_NAME_MAX];
  char symbol_name[DOBBY_ANDROID_NAME_MAX];
  uintptr_t offset;
  void *target_addr;
  void *replace_addr;
  void *origin_addr;
  int enabled;
  int status;
  int backend;
} DobbyAndroidHookRecord;

const char *DobbyAndroidStatusName(int code);
const char *DobbyAndroidGetLastError();
uintptr_t DobbyAndroidGetModuleBase(const char *image_name);
void *DobbyAndroidFindSymbol(const char *image_name, const char *symbol_name);
int DobbyAndroidHookFunction(void *target, void *replace, void **origin);
int DobbyAndroidHookSymbol(const char *image_name, const char *symbol_name, void *replace, void **origin);
int DobbyAndroidHookBySymbolSmart(const char *image_name, const char *symbol_name, void *replace, void **origin);
int DobbyAndroidHookOffset(const char *image_name, uintptr_t offset, void *replace, void **origin);
int DobbyAndroidHookPLT(const char *image_name, const char *symbol,
                         void *replace, void **origin);
int DobbyAndroidHookVtable(void *object, int vtable_index,
                            void *replace, void **origin);
int DobbyAndroidHookShortFunction(void *target, void *replace, void **origin);
void DobbyAndroidEnableNearBranchTrampoline(void);
int DobbyAndroidUnhook(void *target);
int DobbyAndroidIsHooked(void *target);
int DobbyAndroidListHooks(DobbyAndroidHookRecord *records, int max_count);
int DobbyAndroidClearAllHooks();

// import table replace (ELF PLT/GOT alias)
int DobbyImportTableReplace(char *image_name, char *symbol_name, void *fake_func, void **orig_func);

#endif // __linux__ || __ANDROID__

#ifdef __cplusplus
}
#endif

#endif
