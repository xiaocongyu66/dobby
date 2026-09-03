cmake_minimum_required(VERSION 3.10)
project(dobby CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(DOBBY_BUILD_SHARED "Build shared library" ON)
option(DOBBY_STEALTH "Enable stealth (page disguise / jump randomize)" ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/xdl)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

set(SRCS
  src/main.cpp
  src/android_api.cpp
  src/hooker.cpp
  src/events.cpp
  src/linker_monitor.cpp
  src/guard_slot.cpp
  src/callsite.cpp
  src/instrument.cpp
  src/breakpoint.cpp
  src/initarray.cpp
  src/plt_hooker.cpp
  src/waiter.cpp
  src/stealth.cpp
  src/symbols.cpp
  src/util.cpp
  third_party/xdl/xdl.c
  third_party/xdl/xdl_iterate.c
  third_party/xdl/xdl_linker.c
  third_party/xdl/xdl_lzma.c
  third_party/xdl/xdl_util.c
)

if(DOBBY_STEALTH)
  add_compile_definitions(DOBBY_STEALTH=1)
endif()

if(DOBBY_BUILD_SHARED)
  add_library(dobby SHARED ${SRCS})
else()
  add_library(dobby STATIC ${SRCS})
endif()

if(ANDROID)
  target_link_libraries(dobby log)
endif()
