# The approved INF-56 transport/application dictionary inputs are deliberately
# absent from this frozen source tree. Keeping the failure here as well as in
# the top-level option guard prevents this helper from being invoked directly
# to bless the bundled stock QuickFIX dictionaries.
function(irfq_verify_infinite_dictionary_pins dictionary_dir)
  unset(dictionary_dir)
  message(FATAL_ERROR
    "Approved custom Infinite FIXT.1.1 and FIX Latest EP299 dictionary bytes, exact SHA-256 pins, license, and provenance are unavailable; stock QuickFIX dictionaries are prohibited production substitutes")
endfunction()

if("${CMAKE_SCRIPT_MODE_FILE}" STREQUAL "${CMAKE_CURRENT_LIST_FILE}")
  irfq_verify_infinite_dictionary_pins("")
endif()
