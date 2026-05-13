include_directories(.)

set(SOURCE_FILE_LIST
  logging.cc
  )

get_absolute_path_list(SOURCE_FILE_LIST SOURCE_FILE_LIST_)
set(SOURCE_FILE_LIST ${SOURCE_FILE_LIST_})

add_library(logging
  ${SOURCE_FILE_LIST}
  )
