# Android Universal Hook Runtime

Inspired by:
- GlossHook
- ByteDance shadowhook
- ByteHook
- Frida Gum
- SandHook

Core principles:
1. Dobby remains a relocation engine only.
2. Runtime layer owns lifecycle.
3. Linker events trigger auto-rehook.
4. Hook state machine controls safety.
5. Transaction layer avoids inconsistent patch state.
6. ELF parser replaces dlsym-only logic.
7. Runtime targets generic Android, not one engine.

Target compatibility:
- Unity
- UE4
- NativeActivity
- Custom game engines
- Java/JNI hybrid apps
- Hardened APKs
- Zygisk/Riru/LSPosed

Future work:
- PAC/BTI
- stealth trampolines
- execute-only memory support
- anti-detection patch strategies
