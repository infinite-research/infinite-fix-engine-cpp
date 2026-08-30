cmake_minimum_required(VERSION 3.28)

function(_irfq_require variable)
  if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
    message(FATAL_ERROR "${variable} is required; configure it explicitly with -D")
  endif()
endfunction()

function(_irfq_require_regular_file path label)
  if(NOT IS_ABSOLUTE "${path}" OR NOT EXISTS "${path}" OR IS_DIRECTORY "${path}" OR IS_SYMLINK "${path}")
    message(FATAL_ERROR "${label} must be an absolute regular non-symlink file: ${path}")
  endif()
endfunction()

function(_irfq_require_file_identity path expected_size expected_sha256 label)
  file(SIZE "${path}" _size)
  file(SHA256 "${path}" _sha256)
  if(NOT "${_size}" STREQUAL "${expected_size}" OR NOT "${_sha256}" STREQUAL "${expected_sha256}")
    message(FATAL_ERROR "${label} identity mismatch: expected ${expected_size}/${expected_sha256}, got ${_size}/${_sha256}")
  endif()
endfunction()

function(_irfq_probe executable label pattern)
  execute_process(
    COMMAND "${executable}" ${ARGN}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT _result EQUAL 0 OR NOT _output MATCHES "${pattern}")
    message(FATAL_ERROR "${label} does not match the governed version: ${_output}${_error}")
  endif()
endfunction()

foreach(_required IN ITEMS
    IRFQ_PACKAGE_BINARY_DIR
    IRFQ_PACKAGE_OUTPUT_DIR
    IRFQ_PACKAGE_ARCHIVE
    IRFQ_PACKAGE_HEADER
    IRFQ_PACKAGE_ABI_FIXTURE
    IRFQ_PACKAGE_QUICKFIX_LICENSE
    IRFQ_PACKAGE_APACHE_LICENSE
    IRFQ_PACKAGE_DOUBLE_CONVERSION_LICENSE
    IRFQ_PACKAGE_PUGIXML_LICENSE
    IRFQ_PACKAGE_SCOPE_GUARD_LICENSE
    IRFQ_PACKAGE_SOURCE_COMMIT
    IRFQ_PACKAGE_SOURCE_TREE
    IRFQ_PACKAGE_SOURCE_MERGE_BASE
    IRFQ_PACKAGE_SOURCE_DIFF_SHA256
    IRFQ_PACKAGE_SPECIFICATION_COMMIT
    IRFQ_PACKAGE_SPECIFICATION_TREE
    IRFQ_PACKAGE_SPECIFICATION_BUNDLE_SHA256
    IRFQ_PACKAGE_BUILD_IMAGE
    IRFQ_PACKAGE_BUILD_IMAGE_SHA256
    IRFQ_PACKAGE_BUILD_COMMAND
    IRFQ_PACKAGE_C_COMPILER
    IRFQ_PACKAGE_CXX_COMPILER
    IRFQ_PACKAGE_SOURCE_DIR
    IRFQ_PACKAGE_LIB_OUTPUT_DIR
    IRFQ_PACKAGE_ARCHIVE_OUTPUT_DIR
    IRFQ_PACKAGE_ARCHIVE_OUTPUT_DIR_RELEASE
    IRFQ_PACKAGE_GENERATOR
    IRFQ_PACKAGE_NINJA
    IRFQ_PACKAGE_OBJCOPY
    IRFQ_PACKAGE_LINKER
    IRFQ_PACKAGE_AR
    IRFQ_PACKAGE_RANLIB
    IRFQ_PACKAGE_READELF
    IRFQ_PACKAGE_BUILD_TYPE
    IRFQ_PACKAGE_HAVE_SSL
    IRFQ_PACKAGE_QUICKFIX_SHARED_LIBS
    IRFQ_PACKAGE_QUICKFIX_EXAMPLES
    IRFQ_PACKAGE_QUICKFIX_TESTS
    IRFQ_PACKAGE_EMBED_DICTIONARIES
    IRFQ_PACKAGE_HAVE_MYSQL
    IRFQ_PACKAGE_HAVE_POSTGRESQL
    IRFQ_PACKAGE_HAVE_ODBC
    IRFQ_PACKAGE_HAVE_PYTHON3)
  _irfq_require(${_required})
endforeach()
foreach(_required_empty IN ITEMS
    IRFQ_PACKAGE_C_FLAGS
    IRFQ_PACKAGE_C_FLAGS_RELEASE
    IRFQ_PACKAGE_CXX_FLAGS
    IRFQ_PACKAGE_CXX_FLAGS_RELEASE
    IRFQ_PACKAGE_STATIC_LINKER_FLAGS
    IRFQ_PACKAGE_STATIC_LINKER_FLAGS_RELEASE
    IRFQ_PACKAGE_POSITION_INDEPENDENT_CODE
    IRFQ_PACKAGE_INTERPROCEDURAL_OPTIMIZATION
    IRFQ_PACKAGE_INTERPROCEDURAL_OPTIMIZATION_RELEASE
    IRFQ_PACKAGE_UNITY_BUILD
    IRFQ_PACKAGE_CXX_EXTENSIONS
    IRFQ_PACKAGE_TOOLCHAIN_FILE
    IRFQ_PACKAGE_SYSROOT
    IRFQ_PACKAGE_C_COMPILER_LAUNCHER
    IRFQ_PACKAGE_CXX_COMPILER_LAUNCHER)
  if(NOT DEFINED ${_required_empty})
    message(FATAL_ERROR "${_required_empty} is required; configure it explicitly with -D")
  endif()
endforeach()

if(NOT CMAKE_VERSION STREQUAL "3.28.3")
  message(FATAL_ERROR "packaging requires CMake 3.28.3, got ${CMAKE_VERSION}")
endif()
set(_irfq_self_test_mode FALSE)
if(DEFINED IRFQ_PACKAGE_SELF_TEST)
  if(NOT IRFQ_PACKAGE_SELF_TEST STREQUAL "ON")
    message(FATAL_ERROR "IRFQ_PACKAGE_SELF_TEST, when present, must equal ON")
  endif()
  set(_irfq_self_test_mode TRUE)
endif()
if(_irfq_self_test_mode)
  if(NOT IRFQ_PACKAGE_BINARY_DIR MATCHES "^/build/irfq-package-self-test-[A-Za-z0-9_.-]+/binary$")
    message(FATAL_ERROR "package self-test binary directory is not isolated under /build")
  endif()
elseif(NOT IRFQ_PACKAGE_BINARY_DIR STREQUAL "/build")
  message(FATAL_ERROR "packaging requires the governed /build binary path")
endif()
if(NOT IRFQ_PACKAGE_SOURCE_DIR STREQUAL "/src"
   OR NOT IRFQ_PACKAGE_LIB_OUTPUT_DIR STREQUAL "/build/out"
   OR NOT IRFQ_PACKAGE_ARCHIVE_OUTPUT_DIR STREQUAL "/build/out"
   OR NOT IRFQ_PACKAGE_ARCHIVE_OUTPUT_DIR_RELEASE STREQUAL "/build/out"
   OR NOT IRFQ_PACKAGE_C_COMPILER STREQUAL "/usr/bin/gcc-13"
   OR NOT IRFQ_PACKAGE_CXX_COMPILER STREQUAL "/usr/bin/g++-13"
   OR NOT IRFQ_PACKAGE_NINJA STREQUAL "/usr/bin/ninja"
   OR NOT IRFQ_PACKAGE_OBJCOPY STREQUAL "/usr/bin/objcopy"
   OR NOT IRFQ_PACKAGE_LINKER STREQUAL "/usr/bin/ld"
   OR NOT IRFQ_PACKAGE_AR STREQUAL "/usr/bin/ar"
   OR NOT IRFQ_PACKAGE_RANLIB STREQUAL "/usr/bin/ranlib"
   OR NOT IRFQ_PACKAGE_READELF STREQUAL "/usr/bin/readelf")
  message(FATAL_ERROR "packaging requires the governed source, output, compiler, and tool paths")
endif()
if(NOT IRFQ_PACKAGE_C_FLAGS STREQUAL ""
   OR NOT IRFQ_PACKAGE_C_FLAGS_RELEASE STREQUAL "-O3 -DNDEBUG"
   OR NOT IRFQ_PACKAGE_CXX_FLAGS STREQUAL ""
   OR NOT IRFQ_PACKAGE_CXX_FLAGS_RELEASE STREQUAL "-O3 -DNDEBUG"
   OR NOT IRFQ_PACKAGE_STATIC_LINKER_FLAGS STREQUAL ""
   OR NOT IRFQ_PACKAGE_STATIC_LINKER_FLAGS_RELEASE STREQUAL "")
  message(FATAL_ERROR "packaging rejects non-governed compiler or static-linker flags")
endif()
if(NOT IRFQ_PACKAGE_HAVE_MYSQL STREQUAL "OFF"
   OR NOT IRFQ_PACKAGE_HAVE_POSTGRESQL STREQUAL "OFF"
   OR NOT IRFQ_PACKAGE_HAVE_ODBC STREQUAL "OFF"
   OR NOT IRFQ_PACKAGE_HAVE_PYTHON3 STREQUAL "OFF")
  message(FATAL_ERROR "packaging rejects optional database and language bindings")
endif()
foreach(_unset_build_setting IN ITEMS
    IRFQ_PACKAGE_POSITION_INDEPENDENT_CODE
    IRFQ_PACKAGE_INTERPROCEDURAL_OPTIMIZATION
    IRFQ_PACKAGE_INTERPROCEDURAL_OPTIMIZATION_RELEASE
    IRFQ_PACKAGE_UNITY_BUILD
    IRFQ_PACKAGE_CXX_EXTENSIONS
    IRFQ_PACKAGE_TOOLCHAIN_FILE
    IRFQ_PACKAGE_SYSROOT
    IRFQ_PACKAGE_C_COMPILER_LAUNCHER
    IRFQ_PACKAGE_CXX_COMPILER_LAUNCHER)
  if(NOT "${${_unset_build_setting}}" STREQUAL "")
    message(FATAL_ERROR "packaging rejects non-governed build setting ${_unset_build_setting}")
  endif()
endforeach()
set(_irfq_canonical_build_command
    "cmake -S /src -B /build -G Ninja -DCMAKE_C_COMPILER=/usr/bin/gcc-13 -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 -DCMAKE_BUILD_TYPE=Release -DHAVE_SSL=OFF -DQUICKFIX_SHARED_LIBS=OFF -DQUICKFIX_EXAMPLES=OFF -DQUICKFIX_TESTS=ON -DIRFQ_INFINITE_EMBED_DICTIONARIES=ON -DQUICKFIX_LIB_OUTPUT_DIR=/build/out && cmake --build /build --parallel 2 --target infinite_adapter_package")
if(NOT "${IRFQ_PACKAGE_BUILD_COMMAND}" STREQUAL "${_irfq_canonical_build_command}")
  message(FATAL_ERROR "IRFQ_PACKAGE_BUILD_COMMAND must equal the canonical governed build command")
endif()
if(NOT IS_ABSOLUTE "${IRFQ_PACKAGE_BINARY_DIR}" OR NOT IS_DIRECTORY "${IRFQ_PACKAGE_BINARY_DIR}")
  message(FATAL_ERROR "IRFQ_PACKAGE_BINARY_DIR must be an existing absolute directory")
endif()
cmake_path(NORMAL_PATH IRFQ_PACKAGE_BINARY_DIR OUTPUT_VARIABLE _irfq_binary_dir)
cmake_path(NORMAL_PATH IRFQ_PACKAGE_OUTPUT_DIR OUTPUT_VARIABLE _irfq_output_dir)
set(_irfq_expected_output "${_irfq_binary_dir}/infinite-adapter-package")
if(NOT _irfq_output_dir STREQUAL _irfq_expected_output)
  message(FATAL_ERROR "package output is fixed at ${_irfq_expected_output}")
endif()
set(_irfq_root_marker "${_irfq_binary_dir}/.irfq-package-root")
if(NOT EXISTS "${_irfq_root_marker}" OR IS_SYMLINK "${_irfq_root_marker}")
  message(FATAL_ERROR "refusing to package outside a marked build directory")
endif()
file(READ "${_irfq_root_marker}" _irfq_root_marker_value)
if(NOT _irfq_root_marker_value STREQUAL "irfq.infinite-fix-engine-package-root.v1\n")
  message(FATAL_ERROR "invalid package build-directory marker")
endif()

foreach(_input IN ITEMS
    IRFQ_PACKAGE_ARCHIVE
    IRFQ_PACKAGE_HEADER
    IRFQ_PACKAGE_ABI_FIXTURE
    IRFQ_PACKAGE_QUICKFIX_LICENSE
    IRFQ_PACKAGE_APACHE_LICENSE
    IRFQ_PACKAGE_DOUBLE_CONVERSION_LICENSE
    IRFQ_PACKAGE_PUGIXML_LICENSE
    IRFQ_PACKAGE_SCOPE_GUARD_LICENSE)
  _irfq_require_regular_file("${${_input}}" "${_input}")
  file(SIZE "${${_input}}" _irfq_input_size)
  if(_irfq_input_size EQUAL 0)
    message(FATAL_ERROR "${_input} must not be empty")
  endif()
endforeach()
file(READ "${IRFQ_PACKAGE_ARCHIVE}" _irfq_archive_magic OFFSET 0 LIMIT 8 HEX)
if(NOT _irfq_archive_magic STREQUAL "213c617263683e0a")
  message(FATAL_ERROR "IRFQ_PACKAGE_ARCHIVE must be a self-contained regular archive")
endif()
_irfq_require_file_identity(
  "${IRFQ_PACKAGE_QUICKFIX_LICENSE}" 2073
  04719eefe7adf383d7970d7654caec6f7e542e6427f25d068e7177495fc9b379 "QuickFIX license")
_irfq_require_file_identity(
  "${IRFQ_PACKAGE_APACHE_LICENSE}" 11357
  b40930bbcf80744c86c46a12bc9da056641d722716c378f5659b9e555ef833e1 "FIX Trading Community license")
_irfq_require_file_identity(
  "${IRFQ_PACKAGE_DOUBLE_CONVERSION_LICENSE}" 1484
  003b98cb8450a00a766364970ff62722c8a6c43ceec6827e025f9ff9b7ee6720 "double-conversion license")
_irfq_require_file_identity(
  "${IRFQ_PACKAGE_PUGIXML_LICENSE}" 1066
  92232d3df8539e3e1a27468dcef8a9befdc07308b8f9ea8a50c0e0db3b77d74a "pugixml license")
_irfq_require_file_identity(
  "${IRFQ_PACKAGE_SCOPE_GUARD_LICENSE}" 1210
  95d4a66fb7c748ce57da5e4fcf8c9ae963cc9205b00a7036b94f21170eddcaf7 "scope_guard license")

_irfq_probe("${IRFQ_PACKAGE_C_COMPILER}" "C compiler" "^13\\.3\\.0$" -dumpfullversion)
_irfq_probe("${IRFQ_PACKAGE_CXX_COMPILER}" "C++ compiler" "^13\\.3\\.0$" -dumpfullversion)
if(NOT IRFQ_PACKAGE_GENERATOR STREQUAL "Ninja")
  message(FATAL_ERROR "packaging requires the Ninja generator")
endif()
_irfq_probe("${IRFQ_PACKAGE_NINJA}" ninja "^1\\.11\\.1$" --version)
_irfq_probe("${IRFQ_PACKAGE_OBJCOPY}" objcopy "^GNU objcopy[^\n]* 2\\.42($|\n)" --version)
_irfq_probe("${IRFQ_PACKAGE_LINKER}" linker "^GNU ld[^\n]* 2\\.42($|\n)" --version)
_irfq_probe("${IRFQ_PACKAGE_AR}" ar "^GNU ar[^\n]* 2\\.42($|\n)" --version)
_irfq_probe("${IRFQ_PACKAGE_RANLIB}" ranlib "^GNU ranlib[^\n]* 2\\.42($|\n)" --version)
_irfq_probe("${IRFQ_PACKAGE_READELF}" readelf "^GNU readelf[^\n]* 2\\.42($|\n)" --version)

execute_process(
  COMMAND "${IRFQ_PACKAGE_READELF}" --wide --syms "${IRFQ_PACKAGE_ARCHIVE}"
  RESULT_VARIABLE _irfq_readelf_result
  OUTPUT_VARIABLE _irfq_readelf_output
  ERROR_VARIABLE _irfq_readelf_error)
if(NOT _irfq_readelf_result EQUAL 0)
  message(FATAL_ERROR "readelf could not inspect the archive: ${_irfq_readelf_error}")
endif()
string(REPLACE "\r\n" "\n" _irfq_readelf_output "${_irfq_readelf_output}")
if(_irfq_readelf_output MATCHES "[;\\\\]")
  message(FATAL_ERROR "readelf output contains a symbol-list delimiter")
endif()
string(REPLACE "\n" ";" _irfq_readelf_lines "${_irfq_readelf_output}")
set(_irfq_actual_exports)
foreach(_line IN LISTS _irfq_readelf_lines)
  if(_line MATCHES
     "^[ \t]*[0-9]+:[ \t]+[0-9a-fA-F]+[ \t]+[0-9]+[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+(.+)$")
    set(_irfq_export_type "${CMAKE_MATCH_1}")
    set(_irfq_export_binding "${CMAKE_MATCH_2}")
    set(_irfq_export_visibility "${CMAKE_MATCH_3}")
    set(_irfq_export_index "${CMAKE_MATCH_4}")
    set(_irfq_export_name "${CMAKE_MATCH_5}")
    if(_irfq_export_name MATCHES "^irfq_infinite_"
       AND NOT _irfq_export_binding STREQUAL "LOCAL"
       AND (_irfq_export_visibility STREQUAL "DEFAULT" OR _irfq_export_visibility STREQUAL "PROTECTED")
       AND NOT _irfq_export_index STREQUAL "UND")
      if(NOT _irfq_export_type STREQUAL "FUNC"
         OR NOT _irfq_export_binding STREQUAL "GLOBAL"
         OR NOT _irfq_export_visibility STREQUAL "DEFAULT")
        message(FATAL_ERROR
                "public C ABI symbol must be a GLOBAL DEFAULT function: ${_irfq_export_name} "
                "(${_irfq_export_type} ${_irfq_export_binding} ${_irfq_export_visibility})")
      endif()
      list(APPEND _irfq_actual_exports "${_irfq_export_name}")
    endif()
  endif()
endforeach()
list(SORT _irfq_actual_exports)
set(_irfq_expected_exports
    irfq_infinite_abort_v2
    irfq_infinite_apply_committed_v2
    irfq_infinite_destroy_v2
    irfq_infinite_prepare_v2
    irfq_infinite_resume_v2
    irfq_infinite_scan_v2
    irfq_infinite_session_create_v2)
if(NOT "${_irfq_actual_exports}" STREQUAL "${_irfq_expected_exports}")
  message(FATAL_ERROR "archive C ABI exports mismatch: ${_irfq_actual_exports}")
endif()

set(_irfq_stage_dir "${_irfq_binary_dir}/infinite-adapter-package.staging")
foreach(_replaceable IN ITEMS "${_irfq_stage_dir}" "${_irfq_output_dir}")
  if(IS_SYMLINK "${_replaceable}")
    message(FATAL_ERROR "refusing to replace package symlink: ${_replaceable}")
  endif()
endforeach()
file(REMOVE_RECURSE "${_irfq_stage_dir}")
file(MAKE_DIRECTORY "${_irfq_stage_dir}")
file(COPY_FILE "${IRFQ_PACKAGE_ARCHIVE}" "${_irfq_stage_dir}/libquickfix.a")
file(COPY_FILE "${IRFQ_PACKAGE_HEADER}" "${_irfq_stage_dir}/InfiniteFrameAdapter.h")
file(COPY_FILE "${IRFQ_PACKAGE_ABI_FIXTURE}" "${_irfq_stage_dir}/infinite-frame-adapter-abi.v2.tsv")

file(READ "${IRFQ_PACKAGE_QUICKFIX_LICENSE}" _irfq_quickfix_license)
file(READ "${IRFQ_PACKAGE_APACHE_LICENSE}" _irfq_apache_license)
file(READ "${IRFQ_PACKAGE_DOUBLE_CONVERSION_LICENSE}" _irfq_double_conversion_license)
file(READ "${IRFQ_PACKAGE_PUGIXML_LICENSE}" _irfq_pugixml_license)
file(READ "${IRFQ_PACKAGE_SCOPE_GUARD_LICENSE}" _irfq_scope_guard_license)
file(WRITE "${_irfq_stage_dir}/LICENSES.txt"
     "===== QuickFIX Software License 1.0 =====\n${_irfq_quickfix_license}===== FIX Trading Community dictionary source: Apache-2.0 =====\n${_irfq_apache_license}===== double-conversion: BSD-3-Clause =====\n${_irfq_double_conversion_license}===== pugixml: MIT =====\n${_irfq_pugixml_license}===== scope_guard: Unlicense =====\n${_irfq_scope_guard_license}")

file(SIZE "${_irfq_stage_dir}/libquickfix.a" _irfq_archive_size)
file(SHA256 "${_irfq_stage_dir}/libquickfix.a" _irfq_archive_hash)
file(SIZE "${_irfq_stage_dir}/InfiniteFrameAdapter.h" _irfq_header_size)
file(SHA256 "${_irfq_stage_dir}/InfiniteFrameAdapter.h" _irfq_header_hash)
file(SIZE "${_irfq_stage_dir}/infinite-frame-adapter-abi.v2.tsv" _irfq_fixture_size)
file(SHA256 "${_irfq_stage_dir}/infinite-frame-adapter-abi.v2.tsv" _irfq_fixture_hash)
file(SIZE "${_irfq_stage_dir}/LICENSES.txt" _irfq_licenses_size)
file(SHA256 "${_irfq_stage_dir}/LICENSES.txt" _irfq_licenses_hash)

set(_irfq_manifest "${_irfq_stage_dir}/manifest.sha256")
file(WRITE "${_irfq_manifest}" "schema=irfq.infinite-fix-engine-artifact.v2\n")
function(_irfq_manifest_line key value)
  file(APPEND "${_irfq_manifest}" "${key}=${value}\n")
endfunction()
_irfq_manifest_line(source_commit "${IRFQ_PACKAGE_SOURCE_COMMIT}")
_irfq_manifest_line(source_tree "${IRFQ_PACKAGE_SOURCE_TREE}")
_irfq_manifest_line(source_merge_base "${IRFQ_PACKAGE_SOURCE_MERGE_BASE}")
_irfq_manifest_line(source_diff_sha256 "${IRFQ_PACKAGE_SOURCE_DIFF_SHA256}")
_irfq_manifest_line(specification_commit "${IRFQ_PACKAGE_SPECIFICATION_COMMIT}")
_irfq_manifest_line(specification_tree "${IRFQ_PACKAGE_SPECIFICATION_TREE}")
_irfq_manifest_line(specification_bundle_sha256 "${IRFQ_PACKAGE_SPECIFICATION_BUNDLE_SHA256}")
_irfq_manifest_line(target x86_64-unknown-linux-gnu)
_irfq_manifest_line(build_image "${IRFQ_PACKAGE_BUILD_IMAGE}")
_irfq_manifest_line(build_image_sha256 "${IRFQ_PACKAGE_BUILD_IMAGE_SHA256}")
_irfq_manifest_line(cc "gcc 13.3.0")
_irfq_manifest_line(cxx "g++ 13.3.0")
_irfq_manifest_line(cmake "cmake 3.28.3")
_irfq_manifest_line(ninja "ninja 1.11.1")
_irfq_manifest_line(objcopy "GNU objcopy 2.42")
_irfq_manifest_line(linker "GNU ld 2.42")
_irfq_manifest_line(ar "GNU ar 2.42")
_irfq_manifest_line(ranlib "GNU ranlib 2.42")
_irfq_manifest_line(build_type "${IRFQ_PACKAGE_BUILD_TYPE}")
_irfq_manifest_line(have_ssl "${IRFQ_PACKAGE_HAVE_SSL}")
_irfq_manifest_line(quickfix_shared_libs "${IRFQ_PACKAGE_QUICKFIX_SHARED_LIBS}")
_irfq_manifest_line(quickfix_examples "${IRFQ_PACKAGE_QUICKFIX_EXAMPLES}")
_irfq_manifest_line(quickfix_tests "${IRFQ_PACKAGE_QUICKFIX_TESTS}")
_irfq_manifest_line(irfq_infinite_embed_dictionaries "${IRFQ_PACKAGE_EMBED_DICTIONARIES}")
_irfq_manifest_line(build_command "${IRFQ_PACKAGE_BUILD_COMMAND}")
_irfq_manifest_line(transport_dictionary_id INFINITE-FIXT11)
_irfq_manifest_line(transport_dictionary_size 9242)
_irfq_manifest_line(transport_dictionary_sha256 75ecae3957f5f5b0cc8613ac8976bb33dfe3e2edf012cdf36087b349ad5f85e5)
_irfq_manifest_line(application_dictionary_id INFINITE-RFQ-1.0.0-EP299)
_irfq_manifest_line(application_dictionary_size 79971)
_irfq_manifest_line(application_dictionary_sha256 d9ce75d206573a391dbcb83a61665f3844916cfd63006d6ab99d645bac6d2551)
_irfq_manifest_line(dictionary_provenance_sha256 18a671069a156f19a9898dcbe7a7e4b283f063116cc20d756bbcf9ef840d2af4)
_irfq_manifest_line(dictionary_license_inventory_sha256 f76489fe025d794f38f394f34569d899235af925ad24444ebba555f09710de31)
_irfq_manifest_line(c_abi_exports "irfq_infinite_abort_v2,irfq_infinite_apply_committed_v2,irfq_infinite_destroy_v2,irfq_infinite_prepare_v2,irfq_infinite_resume_v2,irfq_infinite_scan_v2,irfq_infinite_session_create_v2")
_irfq_manifest_line(c_abi_exports_sha256 90a3b98861d07a4fcc219e098d75bbaf99ba644f8b996c7725bffd5b28e8286e)
_irfq_manifest_line(archive_path libquickfix.a)
_irfq_manifest_line(archive_size "${_irfq_archive_size}")
_irfq_manifest_line(archive_sha256 "${_irfq_archive_hash}")
_irfq_manifest_line(header_path InfiniteFrameAdapter.h)
_irfq_manifest_line(header_size "${_irfq_header_size}")
_irfq_manifest_line(header_sha256 "${_irfq_header_hash}")
_irfq_manifest_line(abi_fixture_path infinite-frame-adapter-abi.v2.tsv)
_irfq_manifest_line(abi_fixture_size "${_irfq_fixture_size}")
_irfq_manifest_line(abi_fixture_sha256 "${_irfq_fixture_hash}")
_irfq_manifest_line(licenses_path LICENSES.txt)
_irfq_manifest_line(licenses_size "${_irfq_licenses_size}")
_irfq_manifest_line(licenses_sha256 "${_irfq_licenses_hash}")

execute_process(
  COMMAND "${CMAKE_COMMAND}"
          "-DIRFQ_PACKAGE_DIR=${_irfq_stage_dir}"
          "-DIRFQ_EXPECTED_ARCHIVE=${IRFQ_PACKAGE_ARCHIVE}"
          "-DIRFQ_EXPECTED_HEADER=${IRFQ_PACKAGE_HEADER}"
          "-DIRFQ_EXPECTED_ABI_FIXTURE=${IRFQ_PACKAGE_ABI_FIXTURE}"
          "-DIRFQ_EXPECTED_QUICKFIX_LICENSE=${IRFQ_PACKAGE_QUICKFIX_LICENSE}"
          "-DIRFQ_EXPECTED_APACHE_LICENSE=${IRFQ_PACKAGE_APACHE_LICENSE}"
          "-DIRFQ_EXPECTED_DOUBLE_CONVERSION_LICENSE=${IRFQ_PACKAGE_DOUBLE_CONVERSION_LICENSE}"
          "-DIRFQ_EXPECTED_PUGIXML_LICENSE=${IRFQ_PACKAGE_PUGIXML_LICENSE}"
          "-DIRFQ_EXPECTED_SCOPE_GUARD_LICENSE=${IRFQ_PACKAGE_SCOPE_GUARD_LICENSE}"
          "-DIRFQ_EXPECTED_SOURCE_COMMIT=${IRFQ_PACKAGE_SOURCE_COMMIT}"
          "-DIRFQ_EXPECTED_SOURCE_TREE=${IRFQ_PACKAGE_SOURCE_TREE}"
          "-DIRFQ_EXPECTED_SOURCE_MERGE_BASE=${IRFQ_PACKAGE_SOURCE_MERGE_BASE}"
          "-DIRFQ_EXPECTED_SOURCE_DIFF_SHA256=${IRFQ_PACKAGE_SOURCE_DIFF_SHA256}"
          "-DIRFQ_EXPECTED_BUILD_COMMAND=${IRFQ_PACKAGE_BUILD_COMMAND}"
          -P "${CMAKE_CURRENT_LIST_DIR}/VerifyInfiniteAdapterPackage.cmake"
  RESULT_VARIABLE _irfq_verify_result
  OUTPUT_VARIABLE _irfq_verify_output
  ERROR_VARIABLE _irfq_verify_error)
if(NOT _irfq_verify_result EQUAL 0)
  file(REMOVE_RECURSE "${_irfq_stage_dir}")
  message(FATAL_ERROR "package verification failed:\n${_irfq_verify_output}${_irfq_verify_error}")
endif()

if(EXISTS "${_irfq_output_dir}")
  if(NOT IS_DIRECTORY "${_irfq_output_dir}")
    message(FATAL_ERROR "package output exists and is not a directory")
  endif()
  file(REMOVE_RECURSE "${_irfq_output_dir}")
endif()
file(RENAME "${_irfq_stage_dir}" "${_irfq_output_dir}" RESULT _irfq_rename_result)
if(NOT _irfq_rename_result STREQUAL "0")
  message(FATAL_ERROR "could not publish package: ${_irfq_rename_result}")
endif()

message(STATUS "Infinite adapter package published: ${_irfq_output_dir}")
