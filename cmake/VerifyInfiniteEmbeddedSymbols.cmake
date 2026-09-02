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
file(REMOVE "${embedded_object}")
if(EXISTS "${embedded_object}")
  message(FATAL_ERROR "Could not remove stale embedded dictionary scratch object")
endif()
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
file(REMOVE "${embedded_object}")
if(EXISTS "${embedded_object}")
  message(FATAL_ERROR "Embedded dictionary scratch object persists after inspection")
endif()
if(NOT sections_result EQUAL 0)
  message(FATAL_ERROR "readelf sections failed: ${sections_stderr}")
endif()
if(NOT sections MATCHES "[^\n]*\\.note\\.GNU-stack[^\n]*")
  message(FATAL_ERROR "Embedded dictionary object has no GNU stack note")
endif()
if(sections MATCHES "[^\n]*\\.note\\.GNU-stack[^\n]*[ \t][A-Z]*X[A-Z]*[ \t]")
  message(FATAL_ERROR "Embedded dictionary object requires an executable stack")
endif()
if(NOT sections MATCHES "[^\n]*\\.rodata[^\n]*[ \t]A[ \t]")
  message(FATAL_ERROR "Embedded dictionary object has no allocated read-only resource section")
endif()
if(sections MATCHES "[^\n]*\\.rodata[^\n]*[ \t]WA?[ \t]")
  message(FATAL_ERROR "Embedded dictionary resource section is writable")
endif()

string(
  REGEX MATCH
  "[^\n]*GLOBAL[^\n]*_binary_[^\n]*_(start|end|size)([ \t\n]|$)"
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
if(NOT localized_count EQUAL 6)
  message(FATAL_ERROR "Expected 6 localized dictionary boundary symbols, found ${localized_count}")
endif()
foreach(symbol IN ITEMS
    _binary_irfq_infinite_fixt11_xml_start
    _binary_irfq_infinite_fixt11_xml_end
    _binary_irfq_infinite_fixt11_xml_size
    _binary_irfq_infinite_rfq_1_0_0_ep299_xml_start
    _binary_irfq_infinite_rfq_1_0_0_ep299_xml_end
    _binary_irfq_infinite_rfq_1_0_0_ep299_xml_size)
  if(NOT symbols MATCHES "[^\n]*LOCAL[ \t]+DEFAULT[^\n]*${symbol}([ \t\n]|$)")
    message(FATAL_ERROR "Embedded dictionary boundary is absent or not local: ${symbol}")
  endif()
endforeach()
if(symbols MATCHES "_binary_[^\n]*(FIX40|FIX41|FIX42|FIX43|FIX44|FIX50|FIX50SP1|FIX50SP2|FIXT11)_xml")
  message(FATAL_ERROR "Stock QuickFIX dictionary resource is embedded")
endif()
if(NOT symbols MATCHES "[^\n]*GLOBAL[ \t]+HIDDEN[^\n]*irfq_infinite_embedded_dictionaries_v1")
  message(FATAL_ERROR "Embedded dictionary table is not hidden")
endif()
if(EXISTS "${embedded_object}")
  message(FATAL_ERROR "Embedded dictionary scratch object persists after successful verification")
endif()
