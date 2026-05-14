#include "cydia_substrate.h"
#include "symbol_finder.h"
#include "stealth_trampoline.h"
#include "anti_debug.h"
#include "anti_frida.h"
#include "anti_substrate.h"
#include "integrity_bypass.h"
#include "inline_patch_bypass.h"
#include "module_scan_bypass.h"
#include "anti_cheat_sdk.h"
#include "hide_module.h"
#include "hide_maps.h"
#include "dobby.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>

// Global state
static bool g_stealth_enabled = false;
static bool g_stealth_trampoline_enabled = false;
static MSLogLevel g_log_level = MSLogLevelNotice;

// Logging implementation
void MSLog(MSLogLevel level, const char *format, ...) {
    if (level > g_log_level) return;
    
    va_list args;
    va_start(args, format);
    
    switch (level) {
        case MSLogLevelError:
            __android_log_vprint(ANDROID_LOG_ERROR, "MSHook", format, args);
            break;
        case MSLogLevelWarning:
            __android_log_vprint(ANDROID_LOG_WARN, "MSHook", format, args);
            break;
        case MSLogLevelNotice:
            __android_log_vprint(ANDROID_LOG_INFO, "MSHook", format, args);
            break;
        case MSLogLevelDebug:
            __android_log_vprint(ANDROID_LOG_DEBUG, "MSHook", format, args);
            break;
    }
    
    va_end(args);
}

// Set log level
void MSSetLogLevel(MSLogLevel level) {
    g_log_level = level;
}

// Initialize stealth framework
int MSInitStealthFramework() {
    if (g_stealth_enabled) return 0;
    
    // Initialize all stealth components
    if (!LinkerSolistHider::Init()) return -1;
    if (!MapsHider::Init()) return -1;
    if (!ModuleScanBypass::Enable()) return -1;
    
    // Initialize symbol finder
    init_enhanced_symbol_finder();
    
    g_stealth_enabled = true;
    return 0;
}

// Cleanup stealth framework
void MSCleanupStealthFramework() {
    if (!g_stealth_enabled) return;
    
    // Cleanup all stealth components
    LinkerSolistHider::Cleanup();
    MapsHider::Cleanup();
    ModuleScanBypass::Disable();
    InlinePatchBypass::Disable();
    IntegrityBypass::Disable();
    
    // Cleanup symbol finder
    cleanup_enhanced_symbol_finder();
    
    g_stealth_enabled = false;
    g_stealth_trampoline_enabled = false;
}

// Enable full stealth mode
int MSEnableFullStealth() {
    if (!g_stealth_enabled) {
        if (MSInitStealthFramework() != 0) return -1;
    }
    
    // Enable all stealth features
    if (AntiDebug::Enable() != 0) return -1;
    if (AntiFrida::Enable() != 0) return -1;
    if (AntiSubstrate::Enable() != 0) return -1;
    if (InlinePatchBypass::Enable() != 0) return -1;
    if (IntegrityBypass::Enable(nullptr) != 0) return -1;
    
    // Hide all hooked modules
    DobbyAndroidHideAllHookedModules();
    
    // Hide tracer pid
    DobbyAndroidHideTracerPid();
    
    return 0;
}

// Disable stealth mode
void MSDisableStealth() {
    AntiDebug::Cleanup();
    AntiFrida::Cleanup();
    AntiSubstrate::Cleanup();
    InlinePatchBypass::Disable();
    IntegrityBypass::Disable();
    MapsHider::Cleanup();
    ModuleScanBypass::Disable();
    
    g_stealth_enabled = false;
    g_stealth_trampoline_enabled = false;
}

// Check if stealth is enabled
bool MSIsStealthEnabled() {
    return g_stealth_enabled;
}

// Set stealth trampoline
void MSSetStealthTrampoline(bool enable) {
    g_stealth_trampoline_enabled = enable;
    if (enable) {
        StealthTrampoline::Init();
    }
}

// Hook function with stealth capabilities
void MSHookFunction(void *symbol, void *replace, void **result) {
    if (!g_stealth_enabled) {
        // Fallback to basic Dobby hook
        DobbyHook(symbol, replace, result);
        return;
    }
    
    // Use stealth hook
    MSHookFunctionWithStealth(symbol, replace, result);
}

// Hook function with stealth trampoline
void MSHookFunctionWithStealth(void *symbol, void *replace, void **result) {
    if (!g_stealth_enabled) {
        MSInitStealthFramework();
    }
    
    if (g_stealth_trampoline_enabled) {
        // Use stealth trampoline
        // Note: This would require integrating Substrate's trampoline logic
        // For now, use enhanced hook with stealth features
        DobbyHook(symbol, replace, result);
    } else {
        // Use standard stealth hook
        DobbyHook(symbol, replace, result);
    }
}

// Hook PLT entry
int MSHookPLT(const char *image_name, const char *symbol,
              void *replace, void **origin) {
    if (!g_stealth_enabled) {
        MSInitStealthFramework();
    }
    
    return ShortFunctionHook::HookPLT(image_name, symbol, replace, origin);
}

// Hook vtable entry
int MSHookVtable(void *object, int vtable_index,
                void *replace, void **origin) {
    if (!g_stealth_enabled) {
        MSInitStealthFramework();
    }
    
    return ShortFunctionHook::HookVtable(object, vtable_index, replace, origin);
}

// Hook short function
int MSHookShortFunction(void *target, void *replace, void **origin) {
    if (!g_stealth_enabled) {
        MSInitStealthFramework();
    }
    
    return ShortFunctionHook::Hook(target, replace, origin);
}

// Bypass anti-debug
int MSBypassAntiDebug() {
    if (!g_stealth_enabled) {
        MSInitStealthFramework();
    }
    
    return AntiDebug::Enable();
}

// Bypass anti-Frida
int MSBypassAntiFrida() {
    if (!g_stealth_enabled) {
        MSInitStealthFramework();
    }
    
    return AntiFrida::Enable();
}

// Bypass anti-Substrate
int MSBypassAntiSubstrate() {
    if (!g_stealth_enabled) {
        MSInitStealthFramework();
    }
    
    return AntiSubstrate::Enable();
}

// Bypass anti-cheat SDK
int MSBypassAntiCheatSDK(const char *sdk_name) {
    if (!g_stealth_enabled) {
        MSInitStealthFramework();
    }
    
    if (!sdk_name) {
        return AntiCheatSDK::EnableAll();
    } else {
        return AntiCheatSDK::Enable(sdk_name);
    }
}

// Bypass integrity check
int MSBypassIntegrityCheck(const char *image_name) {
    if (!g_stealth_enabled) {
        MSInitStealthFramework();
    }
    
    return IntegrityBypass::Enable(image_name);
}

// Bypass inline patch detection
int MSBypassInlinePatchDetection() {
    if (!g_stealth_enabled) {
        MSInitStealthFramework();
    }
    
    return InlinePatchBypass::Enable();
}

// Bypass module scan
int MSBypassModuleScan() {
    if (!g_stealth_enabled) {
        MSInitStealthFramework();
    }
    
    return ModuleScanBypass::Enable();
}

// Hide module
int MSHideModule(const char *module_name) {
    if (!g_stealth_enabled) {
        MSInitStealthFramework();
    }
    
    return DobbyAndroidHideModule(module_name);
}

// Hide maps entries
int MSHideMapsEntries() {
    if (!g_stealth_enabled) {
        MSInitStealthFramework();
    }
    
    return DobbyAndroidHideMapsInit();
}

// Hide tracer pid
int MSHideTracerPid() {
    if (!g_stealth_enabled) {
        MSInitStealthFramework();
    }
    
    return DobbyAndroidHideTracerPid();
}

// Get symbol address with stealth resolution
void *MSGetSymbolAddress(const char *lib_name, const char *symbol_name) {
    return (void *)enhanced_symbol_resolver(lib_name, symbol_name);
}

// Hook message (class method)
void MSHookMessage(void *class, void *selector, void *replace, void **result) {
    // This would require additional implementation
    // For now, use symbol hooking
    MSHookFunctionWithStealth(selector, replace, result);
}

// Find symbol
void *MSFindSymbol(const char *lib_name, const char *symbol_name) {
    return MSGetSymbolAddress(lib_name, symbol_name);
}