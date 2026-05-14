set(SOURCE_FILE_LIST)

include_directories(
  .
)

if (NOT DEFINED DOBBY_DIR)
  message(FATAL_ERROR "DOBBY_DIR must be set!")
endif ()

if (SYSTEM.Linux OR SYSTEM.Android)
  set(SOURCE_FILE_LIST ${SOURCE_FILE_LIST}
    elf/dobby_symbol_resolver.cc
    dobby_hook_symbol.cc

    ${DOBBY_DIR}/source/Backend/UserMode/PlatformUtil/Linux/ProcessRuntime.cc
    )
  if (SYSTEM.Android)
    set(SOURCE_FILE_LIST ${SOURCE_FILE_LIST}
      elf/dobby_xdl_symbol_resolver.cc
      )
  endif ()
else ()
  message(FATAL_ERROR "SymbolResolver only supports Linux/Android in this fork")
endif ()

get_absolute_path_list(SOURCE_FILE_LIST SOURCE_FILE_LIST_)
set(SOURCE_FILE_LIST ${SOURCE_FILE_LIST_})

add_library(dobby_symbol_resolver
  ${SOURCE_FILE_LIST}
  )
