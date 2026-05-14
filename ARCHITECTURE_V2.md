
# Dobby Runtime V2 Architecture Notes

## Core Principles

1. One relocation engine
2. One hook record system
3. Multiple hook backends
4. Transaction-safe patching
5. Thread-safe lifecycle
6. Android 8-15 compatibility
7. PAC/BTI aware ARM64 support
8. No fake stealth layer

## Required Internal Layers

runtime/
  manager/
  backend/
  transaction/
  symbol/
  memory/

## Recommended Runtime Flow

DobbyHook()
  -> Create HookRecord
  -> Select backend
  -> Validate architecture
  -> Allocate trampoline
  -> Relocate instructions
  -> Patch atomically
  -> Register record
  -> Commit transaction

## Backend Priority

1. Inline Hook
2. Near Inline Hook
3. PLT/GOT Hook
4. VTable Hook

## Required Future Improvements

- Packed relocation support
- PAC stripping/restoration
- BTI trampoline landing pads
- Stop-the-world patching
- Cross-thread PC relocation
- Unified ELF parser
- Hook conflict detection
- Lazy symbol cache

## Things Removed Intentionally

- fake anti-detection
- fake substrate hiding
- unsafe pthread ABI hacks
- unstable linker spoofing

