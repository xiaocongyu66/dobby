#include "hide_maps.h"
#include "dobby.h"

#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <atomic>
#include <string>
#include <vector>
#include <mutex>

namespace dobby_stealth {

std::vector<std::string> MapsHider::hidden_keywords_;
std::vector<std::pair<uintptr_t, uintptr_t>> MapsHider::hidden_ranges_;
bool MapsHider::initialized_ = false;

// ============== 原始函数指针 ==============
static int (*orig_open)(const char *, int, ...) = nullptr;
static int (*orig_openat)(int, const char *, int, ...) = nullptr;
static ssize_t (*orig_read)(int, void *, size_t) = nullptr;
static char *(*orig_fgets)(char *, int, FILE *) = nullptr;
static int (*orig_close)(int) = nullptr;

// ============== 伪 fd 管理 ==============
struct FakeMapsFd {
  int fd;                    // memfd_create 返回的 fd
  std::string content;       // 过滤后的 maps 完整内容
};

static std::mutex g_fake_fd_lock;
static std::vector<FakeMapsFd *> g_fake_fds;

// ============== 判断敏感路径 ==============
static bool is_maps_path(const char *path) {
  if (!path) return false;
  return (strstr(path, "/proc/") && strstr(path, "/maps"));
}

static bool is_status_path(const char *path) {
  if (!path) return false;
  return (strstr(path, "/proc/") && strstr(path, "/status"));
}

// ============== 读取真实 maps 文件 ==============
static std::string read_real_maps() {
  int fd = orig_open("/proc/self/maps", O_RDONLY);
  if (fd < 0) return "";

  std::string content;
  char buf[4096];
  ssize_t n;
  while ((n = orig_read(fd, buf, sizeof(buf) - 1)) > 0) {
    buf[n] = 0;
    content += buf;
  }
  orig_close(fd);
  return content;
}

// ============== 过滤 maps 行 ==============
static std::string filter_maps(const std::string &content) {
  std::string result;
  size_t pos = 0;

  while (pos < content.size()) {
    size_t eol = content.find('\n', pos);
    if (eol == std::string::npos) eol = content.size();

    std::string line = content.substr(pos, eol - pos);
    bool should_hide = false;

    // 关键词过滤
    for (const auto &keyword : hidden_keywords_) {
      if (line.find(keyword) != std::string::npos) {
        should_hide = true;
        break;
      }
    }

    // 地址范围过滤
    if (!should_hide) {
      uintptr_t start = 0, end = 0;
      if (sscanf(line.c_str(), "%lx-%lx", &start, &end) == 2) {
        for (const auto &range : hidden_ranges_) {
          if (start >= range.first && start < range.second) {
            should_hide = true;
            break;
          }
        }
      }
    }

    if (!should_hide) {
      result += line + "\n";
    }

    pos = eol + 1;
  }

  return result;
}

// ============== 从 fd 查找伪 fd ==============
static FakeMapsFd *find_fake_fd(int fd) {
  std::lock_guard<std::mutex> lock(g_fake_fd_lock);
  for (auto *fake : g_fake_fds) {
    if (fake && fake->fd == fd) return fake;
  }
  return nullptr;
}

// ============== 创建伪 fd ==============
static int create_fake_maps_fd() {
  // 读取真实 maps 并过滤
  std::string real_content = read_real_maps();
  std::string filtered = filter_maps(real_content);

  // 使用 memfd_create 创建匿名 fd
  int memfd = syscall(319, "maps", 1); // memfd_create
  if (memfd < 0) {
    // 回退: 使用 pipe
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;
    write(pipefd[1], filtered.c_str(), filtered.size());
    close(pipefd[1]);

    FakeMapsFd *fake = new FakeMapsFd();
    fake->fd = pipefd[0];
    fake->content = filtered;

    std::lock_guard<std::mutex> lock(g_fake_fd_lock);
    g_fake_fds.push_back(fake);
    return pipefd[0];
  }

  // 写入过滤后的内容
  write(memfd, filtered.c_str(), filtered.size());
  lseek(memfd, 0, SEEK_SET);

  FakeMapsFd *fake = new FakeMapsFd();
  fake->fd = memfd;
  fake->content = filtered;

  std::lock_guard<std::mutex> lock(g_fake_fd_lock);
  g_fake_fds.push_back(fake);
  return memfd;
}

// ============== Hook: open ==============
static int hooked_open(const char *path, int flags, ...) {
  mode_t mode = 0;
  if (flags & (O_CREAT | O_TMPFILE)) {
    va_list ap;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
  }

  if (is_maps_path(path)) {
    return create_fake_maps_fd();
  }

  // status 文件需要特殊处理（TracerPid）
  if (is_status_path(path)) {
    // 让原始 open 继续处理，由 fgets hook 来过滤
  }

  return orig_open(path, flags, mode);
}

// ============== Hook: openat ==============
static int hooked_openat(int dirfd, const char *path, int flags, ...) {
  mode_t mode = 0;
  if (flags & (O_CREAT | O_TMPFILE)) {
    va_list ap;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
  }

  if (is_maps_path(path)) {
    return create_fake_maps_fd();
  }

  return orig_openat(dirfd, path, flags, mode);
}

// ============== Hook: close ==============
static int hooked_close(int fd) {
  // 如果是我们创建的伪 fd，需要清理
  {
    std::lock_guard<std::mutex> lock(g_fake_fd_lock);
    for (auto it = g_fake_fds.begin(); it != g_fake_fds.end(); ++it) {
      if ((*it)->fd == fd) {
        delete *it;
        g_fake_fds.erase(it);
        break;
      }
    }
  }
  return orig_close(fd);
}

// ============== Hook: fgets (用于 status 文件过滤 TracerPid) ==============
static char *hooked_fgets(char *buf, int size, FILE *stream) {
  char *ret = orig_fgets(buf, size, stream);
  if (!ret) return nullptr;

  // 过滤 TracerPid 行
  if (strstr(buf, "TracerPid:")) {
    // 替换为 TracerPid:\t0
    snprintf(buf, size, "TracerPid:\t0\n");
  }

  return ret;
}

// ============== 公共 API 实现 ==============

bool MapsHider::Init() {
  if (initialized_) return true;

  // 添加默认隐藏关键词
  AddHiddenKeyword("dobby");
  AddHiddenKeyword("substrate");
  AddHiddenKeyword("frida");

  // Hook open
  void *open_addr = DobbySymbolResolver("libc.so", "open");
  if (open_addr) {
    DobbyHook(open_addr, (void *)hooked_open, (void **)&orig_open);
  }

  // Hook openat
  void *openat_addr = DobbySymbolResolver("libc.so", "openat");
  if (openat_addr) {
    DobbyHook(openat_addr, (void *)hooked_openat, (void **)&orig_openat);
  }

  // Hook close
  void *close_addr = DobbySymbolResolver("libc.so", "close");
  if (close_addr) {
    DobbyHook(close_addr, (void *)hooked_close, (void **)&orig_close);
  }

  // Hook fgets (for TracerPid)
  void *fgets_addr = DobbySymbolResolver("libc.so", "fgets");
  if (fgets_addr) {
    DobbyHook(fgets_addr, (void *)hooked_fgets, (void **)&orig_fgets);
  }

  initialized_ = true;
  return true;
}

void MapsHider::AddHiddenKeyword(const char *keyword) {
  if (keyword) {
    hidden_keywords_.push_back(keyword);
  }
}

void MapsHider::AddHiddenRange(uintptr_t start, uintptr_t end) {
  hidden_ranges_.push_back({start, end});
}

void MapsHider::AutoHideHookedModules() {
  // 遍历 Dobby 的 hook 记录，自动添加隐藏关键词
  // 使用 DobbyAndroidListHooks
  const int max_hooks = 256;
  DobbyAndroidHookRecord *records = new DobbyAndroidHookRecord[max_hooks];
  int count = DobbyAndroidListHooks(records, max_hooks);

  for (int i = 0; i < count; i++) {
    if (records[i].image_name[0] != '\0') {
      AddHiddenKeyword(records[i].image_name);
    }
  }

  delete[] records;
}

void MapsHider::Cleanup() {
  if (!initialized_) return;

  // 恢复所有 Hook
  void *open_addr = DobbySymbolResolver("libc.so", "open");
  void *openat_addr = DobbySymbolResolver("libc.so", "openat");
  void *close_addr = DobbySymbolResolver("libc.so", "close");
  void *fgets_addr = DobbySymbolResolver("libc.so", "fgets");

  if (open_addr) DobbyDestroy(open_addr);
  if (openat_addr) DobbyDestroy(openat_addr);
  if (close_addr) DobbyDestroy(close_addr);
  if (fgets_addr) DobbyDestroy(fgets_addr);

  // 清理伪 fd
  {
    std::lock_guard<std::mutex> lock(g_fake_fd_lock);
    for (auto *fake : g_fake_fds) {
      if (fake) {
        orig_close(fake->fd);
        delete fake;
      }
    }
    g_fake_fds.clear();
  }

  hidden_keywords_.clear();
  hidden_ranges_.clear();
  initialized_ = false;
}

} // namespace dobby_stealth
