foreach(required IN ITEMS IRFQ_INFINITE_SOURCE_DIR IRFQ_INFINITE_BINARY_DIR IRFQ_INFINITE_TEST_CASE)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

if(NOT IRFQ_INFINITE_BINARY_DIR MATCHES "infinite-embedded-")
  message(FATAL_ERROR "Refusing unexpected test build directory: ${IRFQ_INFINITE_BINARY_DIR}")
endif()

file(REMOVE_RECURSE "${IRFQ_INFINITE_BINARY_DIR}")
set(test_source "${IRFQ_INFINITE_SOURCE_DIR}")
set(build_type Release)
set(have_ssl ON)
set(shared OFF)
if(IRFQ_INFINITE_TEST_CASE STREQUAL "DEBUG")
  set(build_type Debug)
  set(expected "requires CMAKE_BUILD_TYPE=Release")
elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "NO_SSL")
  set(have_ssl OFF)
  set(expected "requires HAVE_SSL=ON")
elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "SHARED")
  set(shared ON)
  set(expected "requires QUICKFIX_SHARED_LIBS=OFF")
elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "NON_LINUX")
  set(system_name Generic)
  set(system_processor x86_64)
  set(expected "requires Linux amd64")
elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "NON_AMD64")
  set(system_name Linux)
  set(system_processor aarch64)
  set(expected "requires Linux amd64")
elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "DICTIONARY_DRIFT")
  include("${IRFQ_INFINITE_SOURCE_DIR}/cmake/VerifyInfiniteDictionaryPins.cmake")
  set(dictionary_dir "${IRFQ_INFINITE_BINARY_DIR}/spec")
  file(MAKE_DIRECTORY "${dictionary_dir}")
  foreach(pin IN LISTS IRFQ_INFINITE_DICTIONARY_PINS)
    string(REPLACE "=" ";" fields "${pin}")
    list(GET fields 0 name)
    file(COPY "${IRFQ_INFINITE_SOURCE_DIR}/spec/${name}" DESTINATION "${dictionary_dir}")
  endforeach()
  file(APPEND "${dictionary_dir}/FIX40.xml" "\n")
  set(configure
      "${CMAKE_COMMAND}"
      "-DIRFQ_INFINITE_DICTIONARY_DIR=${dictionary_dir}"
      -P "${IRFQ_INFINITE_SOURCE_DIR}/cmake/VerifyInfiniteDictionaryPins.cmake")
  set(expected "FIX40.xml SHA-256 mismatch")
else()
  message(FATAL_ERROR "Unknown test case: ${IRFQ_INFINITE_TEST_CASE}")
endif()

if(NOT IRFQ_INFINITE_TEST_CASE STREQUAL "DICTIONARY_DRIFT")
  set(configure
      "${CMAKE_COMMAND}"
      -S "${test_source}"
      -B "${IRFQ_INFINITE_BINARY_DIR}"
      "-DCMAKE_BUILD_TYPE=${build_type}"
      "-DCMAKE_INSTALL_PREFIX=${IRFQ_INFINITE_BINARY_DIR}/stage"
      "-DQUICKFIX_LIB_OUTPUT_DIR=${IRFQ_INFINITE_BINARY_DIR}/lib"
      "-DQUICKFIX_SHARED_LIBS=${shared}"
      -DQUICKFIX_EXAMPLES=OFF
      -DQUICKFIX_TESTS=OFF
      "-DHAVE_SSL=${have_ssl}"
      -DIRFQ_INFINITE_EMBED_DICTIONARIES=ON)
  if(DEFINED system_name)
    list(APPEND configure
         "-DCMAKE_SYSTEM_NAME=${system_name}"
         "-DCMAKE_SYSTEM_PROCESSOR=${system_processor}"
         -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY)
  endif()
  if(DEFINED IRFQ_INFINITE_GENERATOR)
    list(APPEND configure -G "${IRFQ_INFINITE_GENERATOR}")
  endif()
  if(DEFINED IRFQ_INFINITE_MAKE_PROGRAM)
    list(APPEND configure "-DCMAKE_MAKE_PROGRAM=${IRFQ_INFINITE_MAKE_PROGRAM}")
  endif()
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
