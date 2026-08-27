foreach(required IN ITEMS IRFQ_INFINITE_ARCHIVE IRFQ_INFINITE_READELF)
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
