function(irfq_verify_infinite_dictionary_pins dictionary_dir)
  set(pins
      "infinite-rfq-1.0.0/INFINITE-FIXT11.xml|9242|75ecae3957f5f5b0cc8613ac8976bb33dfe3e2edf012cdf36087b349ad5f85e5"
      "infinite-rfq-1.0.0/INFINITE-RFQ-1.0.0-EP299.xml|79971|d9ce75d206573a391dbcb83a61665f3844916cfd63006d6ab99d645bac6d2551"
      "infinite-rfq-1.0.0/provenance.v1.json|2754|18a671069a156f19a9898dcbe7a7e4b283f063116cc20d756bbcf9ef840d2af4"
      "infinite-rfq-1.0.0/license-inventory.v1.json|1076|f76489fe025d794f38f394f34569d899235af925ad24444ebba555f09710de31"
      "infinite-rfq-1.0.0/licenses/FIXTradingCommunity-orchestrations-Apache-2.0.txt|11357|b40930bbcf80744c86c46a12bc9da056641d722716c378f5659b9e555ef833e1")
  set(governed_inputs)
  foreach(pin IN LISTS pins)
    string(REPLACE "|" ";" fields "${pin}")
    list(GET fields 0 name)
    list(GET fields 1 expected_size)
    list(GET fields 2 expected_sha256)
    set(path "${dictionary_dir}/${name}")
    if(NOT EXISTS "${path}")
      message(FATAL_ERROR "${name} is missing")
    endif()
    file(SIZE "${path}" actual_size)
    if(NOT actual_size EQUAL expected_size)
      message(FATAL_ERROR "${name} size mismatch: expected ${expected_size}, found ${actual_size}")
    endif()
    file(SHA256 "${path}" actual_sha256)
    if(NOT actual_sha256 STREQUAL expected_sha256)
      message(FATAL_ERROR "${name} SHA-256 mismatch: expected ${expected_sha256}, found ${actual_sha256}")
    endif()
    list(APPEND governed_inputs "${name}")
  endforeach()
  set(IRFQ_INFINITE_GOVERNED_INPUTS "${governed_inputs}" PARENT_SCOPE)
  set(IRFQ_INFINITE_DICTIONARY_RESOURCES
      "infinite-rfq-1.0.0/INFINITE-FIXT11.xml|irfq_infinite_fixt11_xml"
      "infinite-rfq-1.0.0/INFINITE-RFQ-1.0.0-EP299.xml|irfq_infinite_rfq_1_0_0_ep299_xml"
      PARENT_SCOPE)
endfunction()

if("${CMAKE_SCRIPT_MODE_FILE}" STREQUAL "${CMAKE_CURRENT_LIST_FILE}")
  if(NOT DEFINED IRFQ_INFINITE_DICTIONARY_DIR)
    message(FATAL_ERROR "IRFQ_INFINITE_DICTIONARY_DIR is required")
  endif()
  irfq_verify_infinite_dictionary_pins("${IRFQ_INFINITE_DICTIONARY_DIR}")
endif()
