if(NOT DEFINED INPUT_FILE)
  message(FATAL_ERROR "INPUT_FILE is required")
endif()

if(NOT EXISTS "${INPUT_FILE}")
  message(FATAL_ERROR "Input file does not exist: ${INPUT_FILE}")
endif()

file(READ "${INPUT_FILE}" file_content)

set(include_line "#include <QtProtobufWellKnownTypes/private/qprotobufwellknowntypesjsonserializers_p.h>\n")
string(REPLACE "${include_line}" "" updated_content "${file_content}")

if(NOT updated_content STREQUAL file_content)
  file(WRITE "${INPUT_FILE}" "${updated_content}")
endif()
