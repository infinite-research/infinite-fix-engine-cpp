cmake_minimum_required(VERSION 3.12 FATAL_ERROR)

set (quickfix_PROJECT_NAME quickfix)

add_definitions(-DNOMINMAX)

if(DEFINED CMAKE_INSTALL_PREFIX AND NOT IS_ABSOLUTE "${CMAKE_INSTALL_PREFIX}")
  message(FATAL_ERROR "CMAKE_INSTALL_PREFIX must be an absolute final installation path")
endif()

# Large file support
if(UNIX)
  add_definitions(-D_FILE_OFFSET_BITS=64)
endif()

project(${quickfix_PROJECT_NAME} VERSION 1.16.0 LANGUAGES CXX C)
message("-- Project name ${CMAKE_PROJECT_NAME}")

set(IRFQ_INFINITE_CONFIGURED_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
install(CODE "
  if(NOT \"\${CMAKE_INSTALL_PREFIX}\" STREQUAL \"${IRFQ_INFINITE_CONFIGURED_INSTALL_PREFIX}\")
    message(FATAL_ERROR \"The install prefix must match the configured CMAKE_INSTALL_PREFIX because the static Infinite frame adapter embeds its dictionary path\")
  endif()
")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if( MSVC )
  add_compile_options(/MP)
endif()

# Allow overriding the output directory for libraries and executables.
# Default: <build-root>/lib
set(QUICKFIX_LIB_OUTPUT_DIR "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Top-level output directory for quickfix libraries and runtime files")

# Single-config generators
if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${QUICKFIX_LIB_OUTPUT_DIR}")
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${QUICKFIX_LIB_OUTPUT_DIR}")
endif()
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${QUICKFIX_LIB_OUTPUT_DIR}")
endif()

# Multi-config generators (Visual Studio, Xcode)� set per-config variables
foreach(cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${cfg}" cfg_up)
  if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_up})
    set("CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_up}" "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
  endif()
  if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_up})
    set("CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_up}" "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
  endif()
  if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_up})
    set("CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_up}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
  endif()
endforeach()

if( MSVC AND STATIC_RUNTIME )
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
  set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} /MT")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MT")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

# Call cmake with-DHAVE_SSL=ON to compile with SSL. Similarly for others.
option(HAVE_SSL "Build with SSL")
option(HAVE_MYSQL "Build with MySQL")
option(HAVE_POSTGRESQL "Build with PostgreSQL")
option(HAVE_ODBC "Build with ODBC")
option(HAVE_PYTHON3 "Build with default Python3 version")

# add option to build shared or static
option(QUICKFIX_SHARED_LIBS "Build using shared libraries" ON)
# add option to build other targets
option(QUICKFIX_EXAMPLES "Build quickfix examples" ON)
option(QUICKFIX_TESTS "Build quickfix tests" ON)
option(IRFQ_INFINITE_EMBED_DICTIONARIES "Embed approved FIX dictionaries in the static Infinite adapter archive" OFF)

include(QuickfixPlatformSettings)
include(QuickfixPrebuildSetup)

if (QUICKFIX_TESTS)
  enable_testing()
endif()

if(IRFQ_INFINITE_EMBED_DICTIONARIES)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "IRFQ_INFINITE_EMBED_DICTIONARIES requires CMAKE_BUILD_TYPE=Release")
  endif()
  if(HAVE_SSL)
    message(FATAL_ERROR "IRFQ_INFINITE_EMBED_DICTIONARIES requires HAVE_SSL=OFF")
  endif()
  if(QUICKFIX_SHARED_LIBS)
    message(FATAL_ERROR "IRFQ_INFINITE_EMBED_DICTIONARIES requires QUICKFIX_SHARED_LIBS=OFF")
  endif()
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
     OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    message(FATAL_ERROR "IRFQ_INFINITE_EMBED_DICTIONARIES requires Linux amd64")
  endif()
  if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU"
     OR NOT CMAKE_C_COMPILER_VERSION VERSION_EQUAL "13.3.0"
     OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
     OR NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "13.3.0")
    message(FATAL_ERROR "IRFQ_INFINITE_EMBED_DICTIONARIES requires GCC/G++ 13.3.0")
  endif()

  find_program(IRFQ_INFINITE_OBJCOPY NAMES objcopy)
  find_program(IRFQ_INFINITE_READELF NAMES readelf)
  if(NOT IRFQ_INFINITE_OBJCOPY OR NOT IRFQ_INFINITE_READELF OR NOT CMAKE_LINKER)
    message(FATAL_ERROR "IRFQ_INFINITE_EMBED_DICTIONARIES requires objcopy, readelf, and a linker")
  endif()
  execute_process(
    COMMAND ${IRFQ_INFINITE_OBJCOPY} --version
    RESULT_VARIABLE _irfq_objcopy_result
    OUTPUT_VARIABLE _irfq_objcopy_version)
  if(NOT _irfq_objcopy_result EQUAL 0
     OR NOT _irfq_objcopy_version MATCHES "GNU objcopy[^\n]* 2\\.42(\n|$)")
    message(FATAL_ERROR "IRFQ_INFINITE_EMBED_DICTIONARIES requires GNU objcopy 2.42")
  endif()
  execute_process(
    COMMAND ${CMAKE_LINKER} --version
    RESULT_VARIABLE _irfq_linker_result
    OUTPUT_VARIABLE _irfq_linker_version)
  if(NOT _irfq_linker_result EQUAL 0
     OR NOT _irfq_linker_version MATCHES "GNU ld[^\n]* 2\\.42(\n|$)")
    message(FATAL_ERROR "IRFQ_INFINITE_EMBED_DICTIONARIES requires GNU ld 2.42")
  endif()
  message(STATUS "Infinite adapter toolchain: GCC/G++ 13.3.0; GNU objcopy/ld 2.42")

  include(VerifyInfiniteDictionaryPins)
  irfq_verify_infinite_dictionary_pins("${CMAKE_CURRENT_SOURCE_DIR}/spec")
  foreach(_irfq_name IN LISTS IRFQ_INFINITE_GOVERNED_INPUTS)
    set(_irfq_path "${CMAKE_CURRENT_SOURCE_DIR}/spec/${_irfq_name}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_irfq_path}")
  endforeach()
  unset(_irfq_name)
  unset(_irfq_path)
  unset(_irfq_objcopy_result)
  unset(_irfq_objcopy_version)
  unset(_irfq_linker_result)
  unset(_irfq_linker_version)
endif()

#Make sure that a previous config.h has not undefined HAVE_SSL
if(HAVE_SSL)
# Can set location explicitly, example, cmake -DOPENSSL_ROOT_DIR=/usr/local/ssl -DOPENSSL_LIBRARIES=/usr/local/ssl/lib

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

message("-- OPENSSL_INCLUDE_DIR: ${OPENSSL_INCLUDE_DIR}")
message("-- OPENSSL_LIBRARIES: ${OPENSSL_LIBRARIES}")
message("-- OPENSSL_ROOT_DIR: ${OPENSSL_ROOT_DIR}")

message("-- Building with SSL")
endif()

if(HAVE_MYSQL)
find_package(MySQL REQUIRED)
include_directories(${MYSQL_INCLUDE_DIR})

message("-- Building with MySQL")
endif()

if(HAVE_POSTGRESQL)
find_package(PostgreSQL REQUIRED)
include_directories(${PostgreSQL_INCLUDE_DIRS})

message("-- Building with POSTGRESQL")
endif()

if(HAVE_ODBC)
find_package(ODBC REQUIRED)
add_definitions(-DHAVE_ODBC)
include_directories(SYSTEM ${ODBC_INCLUDE_DIRS})
set(ODBC_LIBRARIES "ODBC::ODBC")

message("-- Building with ODBC")
endif()

if (HAVE_PYTHON3)
  find_package(PythonLibs 3 REQUIRED)
  include_directories(${PYTHON_INCLUDE_DIRS})
endif ()

add_subdirectory(src)

if(IRFQ_INFINITE_EMBED_DICTIONARIES)
  set(IRFQ_INFINITE_SOURCE_RELEASE_BASE 386ce46e917ae494ab6e90b1be90fd421cdbe3f9)
  set(IRFQ_INFINITE_SPECIFICATION_COMMIT "" CACHE STRING "Exact specification commit for the Infinite adapter package")
  set(IRFQ_INFINITE_SPECIFICATION_TREE "" CACHE STRING "Exact specification tree for the Infinite adapter package")
  set(IRFQ_INFINITE_SPECIFICATION_BUNDLE_SHA256 "" CACHE STRING "Exact semantic specification bundle SHA-256")
  set(IRFQ_INFINITE_BUILD_IMAGE "" CACHE STRING "Exact build image name for the Infinite adapter package")
  set(IRFQ_INFINITE_BUILD_IMAGE_SHA256 "" CACHE STRING "Exact build image SHA-256 for the Infinite adapter package")
  set(IRFQ_INFINITE_BUILD_COMMAND "" CACHE STRING "Path-independent governed build command")

  file(WRITE "${CMAKE_BINARY_DIR}/.irfq-package-root"
       "irfq.infinite-fix-engine-package-root.v1\n")
  add_custom_target(infinite_adapter_package
    COMMAND "${CMAKE_COMMAND}"
            "-DIRFQ_INFINITE_ARCHIVE=$<TARGET_FILE:quickfix>"
            "-DIRFQ_INFINITE_AR=${CMAKE_AR}"
            "-DIRFQ_INFINITE_READELF=${IRFQ_INFINITE_READELF}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyInfiniteEmbeddedSymbols.cmake"
    COMMAND "${CMAKE_COMMAND}"
            "-DIRFQ_PACKAGE_BINARY_DIR=${CMAKE_BINARY_DIR}"
            "-DIRFQ_PACKAGE_OUTPUT_DIR=${CMAKE_BINARY_DIR}/infinite-adapter-package"
            "-DIRFQ_PACKAGE_ARCHIVE=$<TARGET_FILE:quickfix>"
            "-DIRFQ_PACKAGE_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/src/C++/InfiniteFrameAdapter.h"
            "-DIRFQ_PACKAGE_ABI_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/infinite-frame-adapter-abi.v2.tsv"
            "-DIRFQ_PACKAGE_QUICKFIX_LICENSE=${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "-DIRFQ_PACKAGE_APACHE_LICENSE=${CMAKE_CURRENT_SOURCE_DIR}/spec/infinite-rfq-1.0.0/licenses/FIXTradingCommunity-orchestrations-Apache-2.0.txt"
            "-DIRFQ_PACKAGE_DOUBLE_CONVERSION_LICENSE=${CMAKE_CURRENT_SOURCE_DIR}/licenses/double-conversion-BSD-3-Clause.txt"
            "-DIRFQ_PACKAGE_PUGIXML_LICENSE=${CMAKE_CURRENT_SOURCE_DIR}/licenses/pugixml-MIT.txt"
            "-DIRFQ_PACKAGE_SCOPE_GUARD_LICENSE=${CMAKE_CURRENT_SOURCE_DIR}/licenses/scope_guard-Unlicense.txt"
            "-DIRFQ_PACKAGE_SOURCE_CHECKOUT=${CMAKE_SOURCE_DIR}"
            "-DIRFQ_PACKAGE_SOURCE_RELEASE_BASE=${IRFQ_INFINITE_SOURCE_RELEASE_BASE}"
            "-DIRFQ_PACKAGE_SPECIFICATION_COMMIT=${IRFQ_INFINITE_SPECIFICATION_COMMIT}"
            "-DIRFQ_PACKAGE_SPECIFICATION_TREE=${IRFQ_INFINITE_SPECIFICATION_TREE}"
            "-DIRFQ_PACKAGE_SPECIFICATION_BUNDLE_SHA256=${IRFQ_INFINITE_SPECIFICATION_BUNDLE_SHA256}"
            "-DIRFQ_PACKAGE_BUILD_IMAGE=${IRFQ_INFINITE_BUILD_IMAGE}"
            "-DIRFQ_PACKAGE_BUILD_IMAGE_SHA256=${IRFQ_INFINITE_BUILD_IMAGE_SHA256}"
            "-DIRFQ_PACKAGE_BUILD_COMMAND=${IRFQ_INFINITE_BUILD_COMMAND}"
            "-DIRFQ_PACKAGE_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DIRFQ_PACKAGE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DIRFQ_PACKAGE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DIRFQ_PACKAGE_LIB_OUTPUT_DIR=${QUICKFIX_LIB_OUTPUT_DIR}"
            "-DIRFQ_PACKAGE_ARCHIVE_OUTPUT_DIR=${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
            "-DIRFQ_PACKAGE_ARCHIVE_OUTPUT_DIR_RELEASE=${CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE}"
            "-DIRFQ_PACKAGE_GENERATOR=${CMAKE_GENERATOR}"
            "-DIRFQ_PACKAGE_NINJA=${CMAKE_MAKE_PROGRAM}"
            "-DIRFQ_PACKAGE_OBJCOPY=${IRFQ_INFINITE_OBJCOPY}"
            "-DIRFQ_PACKAGE_LINKER=${CMAKE_LINKER}"
            "-DIRFQ_PACKAGE_AR=${CMAKE_AR}"
            "-DIRFQ_PACKAGE_RANLIB=${CMAKE_RANLIB}"
            "-DIRFQ_PACKAGE_GIT=/usr/bin/git"
            "-DIRFQ_PACKAGE_SHA256SUM=/usr/bin/sha256sum"
            "-DIRFQ_PACKAGE_READELF=${IRFQ_INFINITE_READELF}"
            "-DIRFQ_PACKAGE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "-DIRFQ_PACKAGE_HAVE_SSL=${HAVE_SSL}"
            "-DIRFQ_PACKAGE_QUICKFIX_SHARED_LIBS=${QUICKFIX_SHARED_LIBS}"
            "-DIRFQ_PACKAGE_QUICKFIX_EXAMPLES=${QUICKFIX_EXAMPLES}"
            "-DIRFQ_PACKAGE_QUICKFIX_TESTS=${QUICKFIX_TESTS}"
            "-DIRFQ_PACKAGE_EMBED_DICTIONARIES=${IRFQ_INFINITE_EMBED_DICTIONARIES}"
            "-DIRFQ_PACKAGE_HAVE_MYSQL=${HAVE_MYSQL}"
            "-DIRFQ_PACKAGE_HAVE_POSTGRESQL=${HAVE_POSTGRESQL}"
            "-DIRFQ_PACKAGE_HAVE_ODBC=${HAVE_ODBC}"
            "-DIRFQ_PACKAGE_HAVE_PYTHON3=${HAVE_PYTHON3}"
            "-DIRFQ_PACKAGE_POSITION_INDEPENDENT_CODE=${CMAKE_POSITION_INDEPENDENT_CODE}"
            "-DIRFQ_PACKAGE_INTERPROCEDURAL_OPTIMIZATION=${CMAKE_INTERPROCEDURAL_OPTIMIZATION}"
            "-DIRFQ_PACKAGE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=${CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE}"
            "-DIRFQ_PACKAGE_UNITY_BUILD=${CMAKE_UNITY_BUILD}"
            "-DIRFQ_PACKAGE_CXX_EXTENSIONS=${CMAKE_CXX_EXTENSIONS}"
            "-DIRFQ_PACKAGE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
            "-DIRFQ_PACKAGE_SYSROOT=${CMAKE_SYSROOT}"
            "-DIRFQ_PACKAGE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}"
            "-DIRFQ_PACKAGE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}"
            "-DIRFQ_PACKAGE_C_FLAGS=${CMAKE_C_FLAGS}"
            "-DIRFQ_PACKAGE_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}"
            "-DIRFQ_PACKAGE_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
            "-DIRFQ_PACKAGE_CXX_FLAGS_RELEASE=${CMAKE_CXX_FLAGS_RELEASE}"
            "-DIRFQ_PACKAGE_STATIC_LINKER_FLAGS=${CMAKE_STATIC_LINKER_FLAGS}"
            "-DIRFQ_PACKAGE_STATIC_LINKER_FLAGS_RELEASE=${CMAKE_STATIC_LINKER_FLAGS_RELEASE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageInfiniteAdapter.cmake"
    BYPRODUCTS
      "${CMAKE_BINARY_DIR}/infinite-adapter-package/libquickfix.a"
      "${CMAKE_BINARY_DIR}/infinite-adapter-package/InfiniteFrameAdapter.h"
      "${CMAKE_BINARY_DIR}/infinite-adapter-package/infinite-frame-adapter-abi.v2.tsv"
      "${CMAKE_BINARY_DIR}/infinite-adapter-package/LICENSES.txt"
      "${CMAKE_BINARY_DIR}/infinite-adapter-package/manifest.sha256"
    DEPENDS
      quickfix
      "${CMAKE_CURRENT_SOURCE_DIR}/src/C++/InfiniteFrameAdapter.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/infinite-frame-adapter-abi.v2.tsv"
      "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
      "${CMAKE_CURRENT_SOURCE_DIR}/spec/infinite-rfq-1.0.0/licenses/FIXTradingCommunity-orchestrations-Apache-2.0.txt"
      "${CMAKE_CURRENT_SOURCE_DIR}/licenses/double-conversion-BSD-3-Clause.txt"
      "${CMAKE_CURRENT_SOURCE_DIR}/licenses/pugixml-MIT.txt"
      "${CMAKE_CURRENT_SOURCE_DIR}/licenses/scope_guard-Unlicense.txt"
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageInfiniteAdapter.cmake"
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyInfiniteAdapterPackage.cmake"
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyInfiniteEmbeddedSymbols.cmake"
    COMMENT "Packaging governed Infinite adapter artifact"
    VERBATIM)
endif()

if (QUICKFIX_EXAMPLES)
  add_subdirectory(examples)
endif()

if (QUICKFIX_TESTS)
  if( WIN32)
    add_subdirectory(test)
  endif()
endif()

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/include/quickfix")
configure_file(cmake_config.h.in "${PROJECT_BINARY_DIR}/include/quickfix/config.h" @ONLY)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/spec/ DESTINATION share/quickfix FILES_MATCHING PATTERN "FIX*.xml")
