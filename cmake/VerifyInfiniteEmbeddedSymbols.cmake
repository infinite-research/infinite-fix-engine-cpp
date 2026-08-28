foreach(required IN ITEMS IRFQ_INFINITE_ARCHIVE IRFQ_INFINITE_AR IRFQ_INFINITE_READELF)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

execute_process(
  COMMAND "${IRFQ_INFINITE_READELF}" -Ws "${IRFQ_INFINITE_ARCHIVE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE symbols
  ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "readelf failed: ${stderr}")
endif()

set(embedded_object "${CMAKE_CURRENT_BINARY_DIR}/irfq-infinite-dictionaries.o")
execute_process(
  COMMAND "${IRFQ_INFINITE_AR}" p "${IRFQ_INFINITE_ARCHIVE}" dictionaries.o
  RESULT_VARIABLE extract_result
  OUTPUT_FILE "${embedded_object}"
  ERROR_VARIABLE extract_stderr)
if(NOT extract_result EQUAL 0)
  message(FATAL_ERROR "ar failed: ${extract_stderr}")
endif()
execute_process(
  COMMAND "${IRFQ_INFINITE_READELF}" -SW "${embedded_object}"
  RESULT_VARIABLE sections_result
  OUTPUT_VARIABLE sections
  ERROR_VARIABLE sections_stderr)
if(NOT sections_result EQUAL 0)
  message(FATAL_ERROR "readelf sections failed: ${sections_stderr}")
endif()
if(NOT sections MATCHES "[^\n]*\\.note\\.GNU-stack[^\n]*")
  message(FATAL_ERROR "Embedded dictionary object has no GNU stack note")
endif()
if(sections MATCHES "[^\n]*\\.note\\.GNU-stack[^\n]*[ \t][A-Z]*X[A-Z]*[ \t]")
  message(FATAL_ERROR "Embedded dictionary object requires an executable stack")
endif()

string(
  REGEX MATCH
  "[^\n]*GLOBAL[ \t]+DEFAULT[^\n]*(_binary_|irfq_infinite_dictionary_)[^\n]*(start|end|size)[^\n]*"
  exposed
  "${symbols}")
if(exposed)
  message(FATAL_ERROR "Embedded resource boundary symbol is globally visible: ${exposed}")
endif()

string(
  REGEX MATCHALL
  "[^\n]*LOCAL[ \t]+DEFAULT[^\n]*_binary_[^\n]*_(start|end|size)[^\n]*"
  localized
  "${symbols}")
list(LENGTH localized localized_count)
if(NOT localized_count EQUAL 27)
  message(FATAL_ERROR "Expected 27 localized dictionary boundary symbols, found ${localized_count}")
endif()
if(NOT symbols MATCHES "[^\n]*GLOBAL[ \t]+HIDDEN[^\n]*irfq_infinite_embedded_dictionaries_v1")
  message(FATAL_ERROR "Embedded dictionary table is not hidden")
endif()
