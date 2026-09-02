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
set(have_ssl OFF)
set(shared OFF)
set(expect_success OFF)
set(drift_test OFF)
if(IRFQ_INFINITE_TEST_CASE STREQUAL "SUPPORTED")
  set(expect_success ON)
elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "DEBUG")
  set(build_type Debug)
  set(expected "requires CMAKE_BUILD_TYPE=Release")
elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "SSL")
  set(have_ssl ON)
  set(expected "requires HAVE_SSL=OFF")
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
elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "WRONG_COMPILER")
  set(project_include "${IRFQ_INFINITE_BINARY_DIR}/wrong-compiler.cmake")
  file(WRITE "${project_include}"
       "set(CMAKE_C_COMPILER_ID GNU)\n"
       "set(CMAKE_C_COMPILER_VERSION 16.2.1)\n"
       "set(CMAKE_CXX_COMPILER_ID GNU)\n"
       "set(CMAKE_CXX_COMPILER_VERSION 16.2.1)\n")
  set(expected "requires GCC/G\\+\\+ 13.3.0")
elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "WRONG_OBJCOPY")
  set(objcopy "${IRFQ_INFINITE_BINARY_DIR}/objcopy")
  file(WRITE "${objcopy}"
       "#!/bin/sh\n"
       "if [ \"$1\" = \"--version\" ]; then\n"
       "  printf '%s\\n' 'GNU objcopy (GNU Binutils) 2.47'\n"
       "else\n"
       "  exec \"${IRFQ_INFINITE_REAL_OBJCOPY}\" \"$@\"\n"
       "fi\n")
  file(CHMOD "${objcopy}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
  set(expected "requires GNU objcopy 2.42")
elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "WRONG_LINKER")
  set(linker "${IRFQ_INFINITE_BINARY_DIR}/ld")
  file(WRITE "${linker}"
       "#!/bin/sh\n"
       "if [ \"$1\" = \"--version\" ]; then\n"
       "  printf '%s\\n' 'GNU ld (GNU Binutils) 2.47'\n"
       "else\n"
       "  exec \"${IRFQ_INFINITE_REAL_LINKER}\" \"$@\"\n"
       "fi\n")
  file(CHMOD "${linker}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
  set(expected "requires GNU ld 2.42")
elseif(IRFQ_INFINITE_TEST_CASE MATCHES "^DRIFT_")
  set(drift_test ON)
  set(dictionary_dir "${IRFQ_INFINITE_BINARY_DIR}/spec")
  set(governed_inputs
      infinite-rfq-1.0.0/INFINITE-FIXT11.xml
      infinite-rfq-1.0.0/INFINITE-RFQ-1.0.0-EP299.xml
      infinite-rfq-1.0.0/provenance.v1.json
      infinite-rfq-1.0.0/license-inventory.v1.json
      infinite-rfq-1.0.0/licenses/FIXTradingCommunity-orchestrations-Apache-2.0.txt)
  foreach(name IN LISTS governed_inputs)
    get_filename_component(parent "${name}" DIRECTORY)
    file(MAKE_DIRECTORY "${dictionary_dir}/${parent}")
    configure_file("${IRFQ_INFINITE_SOURCE_DIR}/spec/${name}" "${dictionary_dir}/${name}" COPYONLY)
  endforeach()
  if(IRFQ_INFINITE_TEST_CASE STREQUAL "DRIFT_TRANSPORT")
    set(drift_name infinite-rfq-1.0.0/INFINITE-FIXT11.xml)
  elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "DRIFT_APPLICATION")
    set(drift_name infinite-rfq-1.0.0/INFINITE-RFQ-1.0.0-EP299.xml)
  elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "DRIFT_PROVENANCE")
    set(drift_name infinite-rfq-1.0.0/provenance.v1.json)
  elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "DRIFT_LICENSE_INVENTORY")
    set(drift_name infinite-rfq-1.0.0/license-inventory.v1.json)
  elseif(IRFQ_INFINITE_TEST_CASE STREQUAL "DRIFT_LICENSE")
    set(drift_name infinite-rfq-1.0.0/licenses/FIXTradingCommunity-orchestrations-Apache-2.0.txt)
  else()
    message(FATAL_ERROR "Unknown drift test case: ${IRFQ_INFINITE_TEST_CASE}")
  endif()
  file(READ "${dictionary_dir}/${drift_name}" drift_content)
  string(SUBSTRING "${drift_content}" 1 -1 drift_tail)
  file(WRITE "${dictionary_dir}/${drift_name}" "X${drift_tail}")
  set(configure
      "${CMAKE_COMMAND}"
      "-DIRFQ_INFINITE_DICTIONARY_DIR=${dictionary_dir}"
      -P "${IRFQ_INFINITE_SOURCE_DIR}/cmake/VerifyInfiniteDictionaryPins.cmake")
  get_filename_component(drift_basename "${drift_name}" NAME)
  set(expected "${drift_basename}[ \t\r\n]+SHA-256 mismatch")
else()
  message(FATAL_ERROR "Unknown test case: ${IRFQ_INFINITE_TEST_CASE}")
endif()

if(NOT drift_test)
  set(configure
      "${CMAKE_COMMAND}"
      -S "${test_source}"
      -B "${IRFQ_INFINITE_BINARY_DIR}"
      "-DCMAKE_BUILD_TYPE=${build_type}"
      "-DCMAKE_C_COMPILER=${IRFQ_INFINITE_C_COMPILER}"
      "-DCMAKE_CXX_COMPILER=${IRFQ_INFINITE_CXX_COMPILER}"
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
  if(DEFINED project_include)
    list(APPEND configure "-DCMAKE_PROJECT_INCLUDE=${project_include}")
  endif()
  if(DEFINED objcopy)
    list(APPEND configure "-DIRFQ_INFINITE_OBJCOPY=${objcopy}")
  endif()
  if(DEFINED linker)
    list(APPEND configure "-DCMAKE_LINKER=${linker}")
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
if(expect_success)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Embedded configure unexpectedly rejected ${IRFQ_INFINITE_TEST_CASE}:\n${log}")
  endif()
else()
  if(result EQUAL 0)
    message(FATAL_ERROR "Embedded configure unexpectedly accepted ${IRFQ_INFINITE_TEST_CASE}")
  endif()
  if(NOT log MATCHES "${expected}")
    message(FATAL_ERROR "Embedded configure failed for the wrong reason:\n${log}")
  endif()
endif()
