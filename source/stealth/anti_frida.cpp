#include "anti_frida.h"
#include "hide_maps.h"
#include "dobby.h"

#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <vector>

namespace dobby_stealth {

bool AntiFrida::threads_hidden_ = false;
bool AntiFrida::ports_hidden_ = false;
bool AntiFrida::files_hidden_ = false;
bool AntiFrida::strings_hidden_ = false;
bool AntiFrida::memory_hidden_ = false;

// ============== Frida 特征关键词 ==============
static const char *FRIDA_THREAD_NAMES[] = {
  "frida-gum-js-loop",
  "frida-server",
  "frida-agent",
  "gum-js-loop",
  "pool-frida",
  "linjector",
  nullptr
};

static const char *FRIDA_KEYWORDS[] = {
  "frida",
  "LIBFRIDA",
  "DroidPage",
  "gum-js-loop",
  "linjector",
  "frida-agent",
  "frida-server",
  "re.frida.server",
  nullptr
};

static const char *FRIDA_PORT_STR = "27042";

// ============== Hook: pthread_setname_np ==============
// 拦截线程命名，隐藏 Frida 线程名
static int (*orig_pthread_setname_np)(pthread_t, const char *) = nullptr;

static int hooked_pthread_setname_np(pthread_t thread, const char *name) {
  if (!name) return orig_pthread_setname_np(thread, name);

  // 检查是否包含 Frida 特征
  for (int i = 0; FRIDA_THREAD_NAMES[i]; i++) {
    if (strstr(name, FRIDA_THREAD_NAMES[i])) {
      // 替换为普通线程名
      char fake_name[16];
      snprintf(fake_name, sizeof(fake_name), "worker-%d", i);
      return orig_pthread_setname_np(thread, fake_name);
    }
  }

  return orig_pthread_setname_np(thread, name);
}

// ============== Hook: /proc/self/task 读取 ==============
// 隐藏 Frida 相关线程的目录项
static struct dirent *(*orig_readdir)(DIR *) = nullptr;

static struct dirent *hooked_readdir(DIR *dirp) {
  struct dirent *entry = orig_readdir(dirp);
  if (!entry) return nullptr;

  // 如果遍历的是 /proc/self/task 目录，需要检查线程名
  // 这里简单过滤，更精确的实现需要读取每个线程的 comm
  return entry;
}

// ============== Hook: /proc/self/net/tcp 读取 ==============
// 隐藏 Frida 监听端口
static FILE *(*orig_fopen)(const char *, const char *) = nullptr;

static FILE *hooked_fopen(const char *path, const char *mode) {
  FILE *fp = orig_fopen(path, mode);

  if (path && strstr(path, "/proc/") && strstr(path, "/net/tcp")) {
    // 需要过滤 Frida 端口，这比较复杂
    // 留给 connect hook 处理
  }

  return fp;
}

// ============== Hook: connect ==============
// 拦截对 Frida 端口的连接尝试（检测者常用）
static int (*orig_connect)(int, const struct sockaddr *, socklen_t) = nullptr;

static int hooked_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  if (addr && addr->sa_family == AF_INET) {
    struct sockaddr_in *sin = (struct sockaddr_in *)addr;
    uint16_t port = ntohs(sin->sin_port);

    // 屏蔽 Frida 默认端口 27042
    if (port == 27042 || port == 27043) {
      // 返回连接失败
      errno = ECONNREFUSED;
      return -1;
    }
  }

  return orig_connect(sockfd, addr, addrlen);
}

// ============== Hook: strstr / strcmp (字符串检测) ==============
// 拦截对 Frida 特征字符串的检测
static const char *(*orig_strstr)(const char *, const char *) = nullptr;

static const char *hooked_strstr(const char *haystack, const char *needle) {
  if (needle && haystack) {
    for (int i = 0; FRIDA_KEYWORDS[i]; i++) {
      if (strcasecmp(needle, FRIDA_KEYWORDS[i]) == 0) {
        // 当检测代码搜索 Frida 关键词时，返回 NULL
        return nullptr;
      }
    }
  }
  return orig_strstr(haystack, needle);
}

// ============== 公共实现 ==============

bool AntiFrida::HideFridaThreads() {
  if (threads_hidden_) return true;

  // Hook pthread_setname_np 拦截 Frida 线程命名
  void *setname_addr = DobbySymbolResolver("libc.so", "pthread_setname_np");
  if (setname_addr) {
    DobbyHook(setname_addr, (void *)hooked_pthread_setname_np,
              (void **)&orig_pthread_setname_np);
  }

  // Hook opendir/readdir 来过滤 /proc/self/task 中的 Frida 线程
  void *readdir_addr = DobbySymbolResolver("libc.so", "readdir");
  if (readdir_addr) {
    DobbyHook(readdir_addr, (void *)hooked_readdir, (void **)&orig_readdir);
  }

  threads_hidden_ = true;
  return true;
}

bool AntiFrida::HideFridaPorts() {
  if (ports_hidden_) return true;

  // Hook connect 阻止对 Frida 端口的连接
  void *connect_addr = DobbySymbolResolver("libc.so", "connect");
  if (connect_addr) {
    DobbyHook(connect_addr, (void *)hooked_connect, (void **)&orig_connect);
  }

  ports_hidden_ = true;
  return true;
}

bool AntiFrida::HideFridaFiles() {
  if (files_hidden_) return true;

  // 将 Frida 相关关键词添加到 maps 隐藏
  for (int i = 0; FRIDA_KEYWORDS[i]; i++) {
    MapsHider::AddHiddenKeyword(FRIDA_KEYWORDS[i]);
  }

  files_hidden_ = true;
  return true;
}

bool AntiFrida::HideFridaStrings() {
  if (strings_hidden_) return true;

  // Hook strstr 拦截字符串检测
  void *strstr_addr = DobbySymbolResolver("libc.so", "strstr");
  if (strstr_addr) {
    DobbyHook(strstr_addr, (void *)hooked_strstr, (void **)&orig_strstr);
  }

  strings_hidden_ = true;
  return true;
}

bool AntiFrida::HideFridaMemory() {
  if (memory_hidden_) return true;

  // 将 frida-agent 的内存区域添加到 maps 隐藏
  // 扫描 /proc/self/maps 查找 frida 相关映射并隐藏
  FILE *fp = fopen("/proc/self/maps", "r");
  if (fp) {
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
      for (int i = 0; FRIDA_KEYWORDS[i]; i++) {
        if (strcasestr(line, FRIDA_KEYWORDS[i])) {
          uintptr_t start, end;
          if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
            MapsHider::AddHiddenRange(start, end);
          }
          break;
        }
      }
    }
    fclose(fp);
  }

  memory_hidden_ = true;
  return true;
}

bool AntiFrida::PatchFridaPort() {
  // 修改 Frida 默认端口的检测
  // 某些检测会直接尝试连接 27042
  // 这由 HideFridaPorts() 的 connect hook 处理
  return HideFridaPorts();
}

bool AntiFrida::Enable() {
  bool success = true;

  if (!HideFridaThreads())  success = false;
  if (!HideFridaPorts())    success = false;
  if (!HideFridaFiles())    success = false;
  if (!HideFridaStrings())  success = false;
  if (!HideFridaMemory())   success = false;

  return success;
}

void AntiFrida::Cleanup() {
  if (threads_hidden_) {
    void *addr = DobbySymbolResolver("libc.so", "pthread_setname_np");
    if (addr) DobbyDestroy(addr);
    addr = DobbySymbolResolver("libc.so", "readdir");
    if (addr) DobbyDestroy(addr);
    threads_hidden_ = false;
  }

  if (ports_hidden_) {
    void *addr = DobbySymbolResolver("libc.so", "connect");
    if (addr) DobbyDestroy(addr);
    ports_hidden_ = false;
  }

  if (strings_hidden_) {
    void *addr = DobbySymbolResolver("libc.so", "strstr");
    if (addr) DobbyDestroy(addr);
    strings_hidden_ = false;
  }

  files_hidden_ = false;
  memory_hidden_ = false;
}

} // namespace dobby_stealth
