if (Plugin.ImportTableReplace)
  message(FATAL_ERROR "Plugin.ImportTableReplace was removed from this Linux/Android-only fork.")
endif ()

if (Plugin.Android.BionicLinkerUtil)
  if (NOT SYSTEM.Android)
    message(FATAL_ERROR "[!] Plugin.Android.BionicLinkerUtil only works on Android.")
  endif ()
  message(STATUS "[Dobby] Enable Plugin.Android.BionicLinkerUtil")
  set(dobby.plugin.SOURCE_FILE_LIST ${dobby.plugin.SOURCE_FILE_LIST}
    BionicLinkerUtil/bionic_linker_util.cc
    )
endif ()
