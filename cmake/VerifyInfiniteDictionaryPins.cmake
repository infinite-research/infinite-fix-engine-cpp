set(IRFQ_INFINITE_DICTIONARY_PINS
    FIX40.xml=b5ea484db2b66d6385413dac4cc57b1a2293cedc3f3ac9e3c5200ec1d2df817b
    FIX41.xml=f29c5a4af3158bd6b857d8e369a4a0a532775b3ef5416b0484e3fc7ef7bc7c1d
    FIX42.xml=de70931a0bbb7c06ee0cd1aed3621a090aea2439dfb7946aa5cd4e481f8cd3fa
    FIX43.xml=87e3b757743cb3070f60ad9b6080dfbd04ac015ef1277d1a14dd9d4af5bead4e
    FIX44.xml=a82655b54363aa9c6d1b2f21f294f1198c0d7125d7b44c26d93d3179f3358425
    FIX50.xml=e0f2097b6440ff34ecae3ade7170bcd033f4c14abb22a57cce7db2005ef2be09
    FIX50SP1.xml=05c1b19e560b51702185d93e3905f596126873be60d54b578d70ea827bdfaf00
    FIX50SP2.xml=7d34e565586dd4096a08691d10e415b5a2fd531a8dadfcfc831daea419d3c3f3
    FIXT11.xml=baf0ef6ddebbbbe32c6d66c00bd4a9bab7ded324c4bbca6a07f42e808090bf20)

function(irfq_verify_infinite_dictionary_pins dictionary_dir)
  set(dictionary_names)
  foreach(pin IN LISTS IRFQ_INFINITE_DICTIONARY_PINS)
    string(REPLACE "=" ";" fields "${pin}")
    list(GET fields 0 name)
    list(GET fields 1 expected)
    list(APPEND dictionary_names "${name}")
    file(SHA256 "${dictionary_dir}/${name}" actual)
    if(NOT "${actual}" STREQUAL "${expected}")
      message(FATAL_ERROR "${name} SHA-256 mismatch: expected ${expected}, got ${actual}")
    endif()
  endforeach()
  set(IRFQ_INFINITE_DICTIONARY_NAMES "${dictionary_names}" PARENT_SCOPE)
endfunction()

if("${CMAKE_SCRIPT_MODE_FILE}" STREQUAL "${CMAKE_CURRENT_LIST_FILE}")
  if(NOT DEFINED IRFQ_INFINITE_DICTIONARY_DIR)
    message(FATAL_ERROR "IRFQ_INFINITE_DICTIONARY_DIR is required")
  endif()
  irfq_verify_infinite_dictionary_pins("${IRFQ_INFINITE_DICTIONARY_DIR}")
endif()
