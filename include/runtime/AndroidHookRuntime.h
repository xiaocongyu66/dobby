#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HOOK_CREATED = 0,
  HOOK_INSTALLED,
  HOOK_DISABLED,
  HOOK_DESTROYED,
} HookState;

typedef struct {
  int id;
  void *target;
  void *replace;
  void *origin;
  HookState state;
} HookHandle;

int AHR_Initialize(void);
int AHR_CreateHook(void *target, void *replace, void **origin);
int AHR_EnableHook(int id);
int AHR_DisableHook(int id);
int AHR_DestroyHook(int id);
int AHR_BeginTransaction(void);
int AHR_CommitTransaction(void);
int AHR_RollbackTransaction(void);

#ifdef __cplusplus
}
#endif
