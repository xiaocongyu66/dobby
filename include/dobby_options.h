// ============================================================
// Dobby v2 — 高级自定义 hook (多参数 Options, 高度自定义)
// ============================================================
#ifndef DOBBY_OPTIONS_H
#define DOBBY_OPTIONS_H
#include "dobby.h"

// flags 位标志
#define DOBBY_HOOK_FORCE_THUMB      (1<<0)
#define DOBBY_HOOK_FORCE_ARM        (1<<1)
#define DOBBY_HOOK_NEAR_TRAMPOLINE  (1<<2)
#define DOBBY_HOOK_DISGUISE_PAGE    (1<<3)
#define DOBBY_HOOK_VERIFY           (1<<4)
#define DOBBY_HOOK_4BYTE            (1<<5)
#define DOBBY_HOOK_NO_ORIG_SAVE     (1<<6)

// 跳转编码
#define DOBBY_JUMP_AUTO  0
#define DOBBY_JUMP_LDR   1
#define DOBBY_JUMP_B     2
#define DOBBY_JUMP_BLX   3

typedef struct DobbyOptions {
  uint32_t flags;
  uint8_t  jump_encoding;
  size_t   head_len_override;
  uint32_t retry_ms;
  uint32_t timeout_ms;
  dobby_wait_cb_t cb;
  void    *user;
} DobbyOptions;

#ifdef __cplusplus
extern "C" {
#endif

int DobbyHookEx2(void *target, void *replace, void **origin, const DobbyOptions *opt);
int DobbyPltHookEx(const char *lib, const char *symbol,
                   void *replace, void **origin, const DobbyOptions *opt);
int DobbyWaitEx(const char *lib, const char *symbol, uintptr_t offset, int use_offset,
                void *replace, void **origin, const DobbyOptions *opt);

#ifdef __cplusplus
}
#endif
#endif
