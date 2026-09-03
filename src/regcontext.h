// ============================================================
// dobby::RegContext — ARM32 寄存器上下文 (instrument 底座)
// 用途: 函数内任意位置 hook 的回调里读写全部寄存器/栈
// ============================================================
#ifndef DOBBY_REGCONTEXT_H
#define DOBBY_REGCONTEXT_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef union {
    uint64_t x;
    uint32_t w[2];
} DobbyXReg;

typedef struct DobbyRegContext {
    union {
        struct {
            uint32_t r[13];   // r0-r12
            uint32_t sp;
            uint32_t lr;
        };
        struct {
            uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12;
            uint32_t SP, LR;
        } named;
    };
    uint32_t pc;
    uint32_t cpsr;
} DobbyRegContext;

#ifdef __cplusplus
}
#endif
#endif
