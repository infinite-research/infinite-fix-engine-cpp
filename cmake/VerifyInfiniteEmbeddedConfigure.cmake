foreach(required IN ITEMS IRFQ_INFINITE_SOURCE_DIR IRFQ_INFINITE_BINARY_DIR IRFQ_INFINITE_TEST_CASE)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

if(NOT IRFQ_INFINITE_BINARY_DIR MATCHES "infinite-embedded-")
  message(FATAL_ERROR "Refusing unexpected test build directory: ${IRFQ_INFINITE_BINARY_DIR}")
endif()

set(test_source "${IRFQ_INFINITE_SOURCE_DIR}")
set(build_type Release)
set(have_ssl ON)
if(IRFQ_INFINITE_TEST_CASE STREQUAL "DEBUG")
  set(build_type Debug)
  set(expected "requires CMAKE_BUILD_TYPE=Release")
elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "NO_SSL")
  set(have_ssl OFF)
  set(expected "requires HAVE_SSL=ON")
elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "DICTIONARY_DRIFT")
  set(test_source "${IRFQ_INFINITE_BINARY_DIR}-source")
  file(REMOVE_RECURSE "${test_source}")
  file(COPY "${IRFQ_INFINITE_SOURCE_DIR}/" DESTINATION "${test_source}" PATTERN ".git" EXCLUDE)
  file(APPEND "${test_source}/spec/FIX40.xml" "\n")
  set(expected "FIX40.xml SHA-256 mismatch")
else()
  message(FATAL_ERROR "Unknown test case: ${IRFQ_INFINITE_TEST_CASE}")
endif()

file(REMOVE_RECURSE "${IRFQ_INFINITE_BINARY_DIR}")
set(configure
    "${CMAKE_COMMAND}"
    -S "${test_source}"
    -B "${IRFQ_INFINITE_BINARY_DIR}"
    "-DCMAKE_BUILD_TYPE=${build_type}"
    "-DCMAKE_INSTALL_PREFIX=${IRFQ_INFINITE_BINARY_DIR}/stage"
    "-DQUICKFIX_LIB_OUTPUT_DIR=${IRFQ_INFINITE_BINARY_DIR}/lib"
    -DQUICKFIX_SHARED_LIBS=OFF
    -DQUICKFIX_EXAMPLES=OFF
    -DQUICKFIX_TESTS=OFF
    "-DHAVE_SSL=${have_ssl}"
    -DIRFQ_INFINITE_EMBED_DICTIONARIES=ON)
if(DEFINED IRFQ_INFINITE_GENERATOR)
  list(APPEND configure -G "${IRFQ_INFINITE_GENERATOR}")
endif()
if(DEFINED IRFQ_INFINITE_MAKE_PROGRAM)
  list(APPEND configure "-DCMAKE_MAKE_PROGRAM=${IRFQ_INFINITE_MAKE_PROGRAM}")
endif()

execute_process(
  COMMAND ${configure}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)
set(log "${stdout}\n${stderr}")
if(result EQUAL 0)
  message(FATAL_ERROR "Embedded configure unexpectedly accepted ${IRFQ_INFINITE_TEST_CASE}")
endif()
if(NOT log MATCHES "${expected}")
  message(FATAL_ERROR "Embedded configure failed for the wrong reason:\n${log}")
endif()
