set(DOBBY_STEALTH_SOURCES
  hide_module.cpp
  hide_maps.cpp
  anti_debug.cpp
  anti_frida.cpp
  anti_substrate.cpp
  integrity_bypass.cpp
  inline_patch_bypass.cpp
  module_scan_bypass.cpp
  anti_cheat_sdk.cpp
  stealth_api.cpp
  short_function_hook.cpp
  stealth_trampoline.cpp
  hook_context.cpp
)

add_library(dobby_stealth STATIC ${DOBBY_STEALTH_SOURCES})

target_include_directories(dobby_stealth PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/source
)

target_link_libraries(dobby_stealth PUBLIC dobby dl)
