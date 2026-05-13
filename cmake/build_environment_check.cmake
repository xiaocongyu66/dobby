if(__BUILD_ENVIRONMENT_CHECK)
  return()
endif()
set(__BUILD_ENVIRONMENT_CHECK TRUE)

message(STATUS "")
message(STATUS "********* build environment check ***********")

# The Compiler ID
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
  set(COMPILER.Clang 1)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set(COMPILER.Gcc 1)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  message(FATAL_ERROR "This fork only supports Linux/Android GCC or Clang builds; MSVC/Windows has been removed.")
else()
  message(FATAL_ERROR "Compiler ${CMAKE_CXX_COMPILER_ID} not configured")
endif()
message(STATUS "\tCompiler: \t ${CMAKE_CXX_COMPILER_ID}")

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" CMAKE_SYSTEM_PROCESSOR)

# The Processor
if(CMAKE_SYSTEM_PROCESSOR MATCHES "amd64.*|x86_64.*|AMD64.*|x64.*")
  set(PROCESSOR.X86_64 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i686.*|i386.*|x86.*")
  set(PROCESSOR.X86 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64.*|aarch64.*)")
  set(PROCESSOR.AARCH64 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm.*)")
  set(PROCESSOR.ARM 1)
else()
  message(FATAL_ERROR "Processor ${CMAKE_SYSTEM_PROCESSOR} not configured")
endif()
message(STATUS "\tProcessor:\t ${CMAKE_SYSTEM_PROCESSOR}")

# The System: Linux + Android only.
if(CMAKE_SYSTEM_NAME MATCHES "^Android")
  set(SYSTEM.Android 1)
elseif(CMAKE_SYSTEM_NAME MATCHES "^Linux")
  set(SYSTEM.Linux 1)
else()
  message(FATAL_ERROR "System ${CMAKE_SYSTEM_NAME} is not supported by this Linux/Android-only fork")
endif()
message(STATUS "\tSystem:   \t ${CMAKE_SYSTEM_NAME}")

message(STATUS "***************************************")
message(STATUS "")
