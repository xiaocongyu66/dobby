#include "hide_module.h"
#include "dobby.h"

#include <dlfcn.h>
#include <link.h>
#include <inttypes.h>
#include <cstring>
#include <cstdio>

namespace dobby_stealth {

bool LinkerSolistHider::initialized_ = false;
std::vector<LinkerSolistHider::HiddenNode> LinkerSolistHider::hidden_nodes_;

// Android linker 内部 soinfo 的前几个字段（简化版）
// 不同 Android 版本布局不同，需要适配
struct soinfo_compat {
  void *next;       // 偏移 0: 指向下一个 soinfo
  // ... 中间字段因版本而异
  // name 偏移因版本而异，我们需要动态探测
};

// 获取 solist 头指针
// Android 8+: linker64 中的全局变量
static void **get_solist_head() {
  // 方法1: 通过符号查找
  // 不同版本的符号名:
  // - __dl__ZL6solist  (Android 8-10)
  // - solist           (某些版本)
  void *linker_handle = dlopen("linker64", RTLD_NOW);
  if (!linker_handle) {
    linker_handle = dlopen("linker", RTLD_NOW);
  }
  if (!linker_handle) return nullptr;

  // 尝试多种符号名
  const char *symbols[] = {
    "__dl__ZL6solist",
    "__dl__ZL8solist",
    "solist",
    "__dl_solist",
    nullptr
  };

  for (int i = 0; symbols[i]; i++) {
    void **head = (void **)dlsym(linker_handle, symbols[i]);
    if (head && *head) {
      dlclose(linker_handle);
      return head;
    }
  }

  dlclose(linker_handle);
  return nullptr;
}

// 获取 soinfo 的 name 字段偏移
// 通过遍历 solist，对比已知模块名来探测
static size_t detect_name_offset(void *node) {
  if (!node) return (size_t)-1;

  // 已知的模块名关键词
  const char *known_names[] = {
    "linker",
    "libc.so",
    "libm.so",
    nullptr
  };

  // 从偏移 8 开始搜索（跳过 next 指针）
  // 最大搜索到 512 字节
  for (size_t off = sizeof(void *); off < 512; off += sizeof(void *)) {
    const char *candidate = *(const char **)((uint8_t *)node + off);
    if (!candidate) continue;

    for (int i = 0; known_names[i]; i++) {
      if (candidate && strstr(candidate, known_names[i])) {
        return off;
      }
    }
  }

  return (size_t)-1;
}

// 全局缓存
static void **g_solist_head = nullptr;
static size_t g_name_offset = (size_t)-1;

// 获取 soinfo 的 next 指针
static void *get_next_node(void *node) {
  return *(void **)node;
}

// 设置 soinfo 的 next 指针
static void set_next_node(void *node, void *next) {
  *(void **)node = next;
}

// 获取 soinfo 的 name
static const char *get_node_name(void *node) {
  if (g_name_offset == (size_t)-1) return nullptr;
  return *(const char **)((uint8_t *)node + g_name_offset);
}

// 在 solist 中查找指定名称的前驱节点
static void *find_prev_node(void *head, const char *name) {
  void *current = *(void **)head;
  void *prev = head; // 前驱的 next 指针位置

  while (current) {
    const char *node_name = get_node_name(current);
    if (node_name && strstr(node_name, name)) {
      return prev;
    }
    prev = current;
    current = get_next_node(current);
  }

  return nullptr;
}

bool LinkerSolistHider::Init() {
  if (initialized_) return true;

  g_solist_head = get_solist_head();
  if (!g_solist_head) return false;

  // 探测 name 偏移
  void *first_node = *g_solist_head;
  if (first_node) {
    g_name_offset = detect_name_offset(first_node);
    if (g_name_offset == (size_t)-1) return false;
  }

  initialized_ = true;
  return true;
}

bool LinkerSolistHider::HideModule(const char *module_name) {
  if (!initialized_ && !Init()) return false;
  if (!module_name) return false;

  // 查找前驱节点
  void *prev = find_prev_node(g_solist_head, module_name);
  if (!prev) return false;

  // 取出当前节点
  void *current;
  if (prev == g_solist_head) {
    current = *g_solist_head;
  } else {
    current = get_next_node(prev);
  }

  if (!current) return false;

  // 保存隐藏信息
  HiddenNode hidden;
  const char *node_name = get_node_name(current);
  hidden.name = node_name ? node_name : module_name;

  // 获取模块基地址（通过 dl_iterate_phdr 回调）
  struct FindBaseCtx {
    const char *name;
    uintptr_t base;
  } ctx = {module_name, 0};

  dl_iterate_phdr([](struct dl_phdr_info *info, size_t size, void *data) -> int {
    auto *ctx = (FindBaseCtx *)data;
    if (info->dlpi_name && strstr(info->dlpi_name, ctx->name)) {
      ctx->base = info->dlpi_addr;
      return 1;
    }
    return 0;
  }, &ctx);

  hidden.base = ctx.base;
  hidden.prev_node = prev;
  hidden.self_node = current;

  // 从链表中摘除
  void *next = get_next_node(current);
  if (prev == g_solist_head) {
    *g_solist_head = next;
  } else {
    set_next_node(prev, next);
  }

  hidden_nodes_.push_back(hidden);
  return true;
}

bool LinkerSolistHider::HideModuleByBase(uintptr_t base) {
  if (!base) return false;

  // 通过 /proc/self/maps 查找基地址对应的模块名
  FILE *fp = fopen("/proc/self/maps", "r");
  if (!fp) return false;

  char line[512];
  char name[256] = {0};
  while (fgets(line, sizeof(line), fp)) {
    uintptr_t start;
    if (sscanf(line, "%" SCNxPTR "-", &start) == 1 && start == base) {
      // 提取名称
      char *p = strrchr(line, '/');
      if (p) {
        strncpy(name, p + 1, sizeof(name) - 1);
        // 去除换行
        name[strcspn(name, "\n")] = 0;
      }
      break;
    }
  }
  fclose(fp);

  if (name[0]) {
    return HideModule(name);
  }
  return false;
}

bool LinkerSolistHider::RestoreModule(const char *module_name) {
  for (auto it = hidden_nodes_.begin(); it != hidden_nodes_.end(); ++it) {
    if (it->name.find(module_name) != std::string::npos) {
      // 重新插入链表
      void *prev = it->prev_node;
      void *node = it->self_node;

      if (prev == g_solist_head) {
        set_next_node(node, *g_solist_head);
        *g_solist_head = node;
      } else {
        set_next_node(node, get_next_node(prev));
        set_next_node(prev, node);
      }

      hidden_nodes_.erase(it);
      return true;
    }
  }
  return false;
}

void LinkerSolistHider::Cleanup() {
  // 恢复所有隐藏的节点
  for (auto &node : hidden_nodes_) {
    void *prev = node.prev_node;
    void *self = node.self_node;

    if (prev == g_solist_head) {
      set_next_node(self, *g_solist_head);
      *g_solist_head = self;
    } else {
      set_next_node(self, get_next_node(prev));
      set_next_node(prev, self);
    }
  }
  hidden_nodes_.clear();
}

} // namespace dobby_stealth
