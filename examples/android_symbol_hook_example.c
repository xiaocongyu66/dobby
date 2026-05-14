#include "dobby.h"

#if defined(__ANDROID__)
#include <android/log.h>
#include <string.h>

#define LOG_TAG "DobbyAndroidDemo"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static int (*orig_strcmp)(const char *a, const char *b) = 0;

static int fake_strcmp(const char *a, const char *b) {
  ALOGI("strcmp called: %s / %s", a ? a : "<null>", b ? b : "<null>");
  return orig_strcmp ? orig_strcmp(a, b) : 0;
}

__attribute__((constructor)) static void install_demo_hook(void) {
  int rc = DobbyAndroidHookSymbol("libc.so", "strcmp", (void *)fake_strcmp, (void **)&orig_strcmp);
  if (rc != DOBBY_ANDROID_OK) {
    ALOGE("DobbyAndroidHookSymbol failed: %s / %s", DobbyAndroidStatusName(rc), DobbyAndroidGetLastError());
    return;
  }

  DobbyAndroidHookRecord records[8];
  int total = DobbyAndroidListHooks(records, 8);
  ALOGI("installed hooks: %d", total);
}
#endif
