
// ============================================================
// ===================== 使用示例 =====================
// ============================================================
//
// 【1. 基础 inline hook】
//   static int (*orig_add)(int, int);
//   int my_add(int a, int b) { return orig_add(a, b) + 1; }
//   DobbyInit(false);
//   DobbyHook((void*)add_addr, (void*)my_add, (void**)&orig_add);
//   DobbyUnhook((void*)add_addr);          // 解除
//
// 【2. 符号 hook (自动解析 dynsym+symtab+gnu_debugdata)】
//   void *p = DobbySymbol("libMSDK.so", "exit");
//   uintptr_t base = DobbyBase("libMSDK.so");
//   DobbyAndroidHookSymbol("libMSDK.so", "exit", my_exit, &orig_exit);
//
// 【3. PLT/GOT hook (对指定库的符号调用点生效)】
//   DobbyPltHook("libtprt.so", "open", my_open, &orig_open);
//   DobbyPltUnhook("libtprt.so", "open");
//
// 【4. 等待 hook (目标库未加载时自动挂起, 加载瞬间装配 — GlossHook 同款)】
//   DobbyWaitHook("libMSDK.so", "exit", my_exit, &orig_exit,
//                 10000, on_hooked, nullptr);        // 10s 超时
//   DobbyWaitHookOffset("libtprt.so", 0x18c6c, patch_fn, &orig,
//                       0, on_hooked, nullptr);      // 永久等待
//
// 【5. 高级自定义 (DobbyOptions — 多参数高度自定义)】
//   DobbyOptions opt = {};
//   opt.flags = DOBBY_HOOK_DISGUISE_PAGE      // trampoline 页改名伪装
//             | DOBBY_HOOK_VERIFY;            // 安装后快照完整性
//   opt.jump_encoding = DOBBY_JUMP_LDR;       // 跳转编码
//   opt.head_len_override = 8;                // 自定义函数头覆盖长度
//   opt.timeout_ms = 5000;                    // wait 超时
//   opt.cb = on_hooked; opt.user = nullptr;
//   DobbyHookEx2((void*)target, my_fn, &orig, &opt);
//   DobbyWaitEx("libMSDK.so", "exit", 0, false, my_exit, &orig, &opt);
//
// 【6. 内存补丁】
//   DobbyMemoryNop(addr, 4);                  // NOP (thumb/arm 自适应)
//   DobbyMemoryWrite(addr, bytes, sizeof(bytes));
//   DobbyMemoryProtect(addr, 0x1000, PROT_READ|PROT_WRITE);
//
// 【7. 反检测】
//   DobbyStealthDisguise(page, 0x1000);       // 匿名页伪装系统段名
//   bool ok = DobbyStealthVerify(addr);       // hook 完整性 (TP unhook 检测)
//
// 【8. 多 hook 共存/事务式管理】
//   // 同一位置可多次 hook; GlossHook/DeleteAll 按地址管理 (gloss_hook_manager)
// ============================================================
