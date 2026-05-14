#include "hook_context.h"

#include <cstring>
#include <string>

namespace dobby_stealth {

#if defined(__aarch64__) || defined(__arm64__)

uint64_t HookContext::GetX(int n) const {
  if (!ctx || n < 0 || n > 28) return 0;
  return ctx->general.x[n];
}

void HookContext::SetX(int n, uint64_t val) {
  if (!ctx || n < 0 || n > 28) return;
  ctx->general.x[n] = val;
}

uint64_t HookContext::GetSP() const {
  return ctx ? ctx->sp : 0;
}

void HookContext::SetSP(uint64_t val) {
  if (ctx) ctx->sp = val;
}

uint64_t HookContext::GetLR() const {
  return ctx ? ctx->lr : 0;
}

void HookContext::SetLR(uint64_t val) {
  if (ctx) ctx->lr = val;
}

uint64_t HookContext::GetFP() const {
  return ctx ? ctx->fp : 0;
}

void HookContext::SetFP(uint64_t val) {
  if (ctx) ctx->fp = val;
}

uint64_t HookContext::ReadStack(int offset) const {
  if (!ctx) return 0;
  uint64_t *addr = (uint64_t *)(ctx->sp + offset);
  return *addr;
}

void HookContext::WriteStack(int offset, uint64_t val) {
  if (!ctx) return;
  uint64_t *addr = (uint64_t *)(ctx->sp + offset);
  *addr = val;
}

uint64_t HookContext::ReadMemory(uint64_t addr) const {
  uint64_t val = 0;
  memcpy(&val, (void *)addr, sizeof(val));
  return val;
}

void HookContext::WriteMemory(uint64_t addr, uint64_t val) {
  memcpy((void *)addr, &val, sizeof(val));
}

std::string HookContext::ReadCString(uint64_t addr) const {
  if (!addr) return "";
  std::string result;
  const char *p = (const char *)addr;
  while (*p) {
    result += *p++;
  }
  return result;
}

#elif defined(__arm__)

uint32_t HookContext::GetR(int n) const {
  if (!ctx || n < 0 || n > 12) return 0;
  return ctx->general.r[n];
}

void HookContext::SetR(int n, uint32_t val) {
  if (!ctx || n < 0 || n > 12) return;
  ctx->general.r[n] = val;
}

uint32_t HookContext::GetSP() const {
  return ctx ? ctx->sp : 0;
}

void HookContext::SetSP(uint32_t val) {
  if (ctx) ctx->sp = val;
}

uint32_t HookContext::GetLR() const {
  return ctx ? ctx->lr : 0;
}

void HookContext::SetLR(uint32_t val) {
  if (ctx) ctx->lr = val;
}

#endif

} // namespace dobby_stealth
