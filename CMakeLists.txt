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
# Default: <project-root>/lib
set(QUICKFIX_LIB_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/lib" CACHE PATH "Top-level output directory for quickfix libraries and runtime files")

# Single-config generators
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${QUICKFIX_LIB_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${QUICKFIX_LIB_OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${QUICKFIX_LIB_OUTPUT_DIR}")

# Multi-config generators (Visual Studio, Xcode)� set per-config variables
foreach(cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${cfg}" cfg_up)
  set("CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_up}" "${QUICKFIX_LIB_OUTPUT_DIR}")
  set("CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_up}" "${QUICKFIX_LIB_OUTPUT_DIR}")
  set("CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_up}" "${QUICKFIX_LIB_OUTPUT_DIR}")
endforeach()

if( MSVC AND STATIC_RUNTIME )
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
  set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} /MT")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MT")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

include(QuickfixPlatformSettings)
include(QuickfixPrebuildSetup)

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
if (QUICKFIX_TESTS)
  enable_testing()
endif()

if(IRFQ_INFINITE_EMBED_DICTIONARIES)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "IRFQ_INFINITE_EMBED_DICTIONARIES requires CMAKE_BUILD_TYPE=Release")
  endif()
  if(NOT HAVE_SSL)
    message(FATAL_ERROR "IRFQ_INFINITE_EMBED_DICTIONARIES requires HAVE_SSL=ON")
  endif()
  if(QUICKFIX_SHARED_LIBS)
    message(FATAL_ERROR "IRFQ_INFINITE_EMBED_DICTIONARIES requires QUICKFIX_SHARED_LIBS=OFF")
  endif()
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
     OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    message(FATAL_ERROR "IRFQ_INFINITE_EMBED_DICTIONARIES requires Linux amd64")
  endif()

  find_program(IRFQ_INFINITE_OBJCOPY NAMES objcopy)
  find_program(IRFQ_INFINITE_READELF NAMES readelf)
  if(NOT IRFQ_INFINITE_OBJCOPY OR NOT IRFQ_INFINITE_READELF OR NOT CMAKE_LINKER)
    message(FATAL_ERROR "IRFQ_INFINITE_EMBED_DICTIONARIES requires objcopy, readelf, and a linker")
  endif()
  execute_process(COMMAND ${IRFQ_INFINITE_OBJCOPY} --version OUTPUT_VARIABLE _irfq_objcopy_version)
  if(NOT _irfq_objcopy_version MATCHES "GNU objcopy")
    message(FATAL_ERROR "IRFQ_INFINITE_EMBED_DICTIONARIES requires GNU objcopy")
  endif()
  unset(_irfq_objcopy_version)

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
  set(IRFQ_INFINITE_DICTIONARY_NAMES)
  foreach(_irfq_pin IN LISTS IRFQ_INFINITE_DICTIONARY_PINS)
    string(REPLACE "=" ";" _irfq_fields "${_irfq_pin}")
    list(GET _irfq_fields 0 _irfq_name)
    list(GET _irfq_fields 1 _irfq_expected)
    set(_irfq_path "${CMAKE_CURRENT_SOURCE_DIR}/spec/${_irfq_name}")
    list(APPEND IRFQ_INFINITE_DICTIONARY_NAMES "${_irfq_name}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_irfq_path}")
    file(SHA256 "${_irfq_path}" _irfq_actual)
    if(NOT _irfq_actual STREQUAL _irfq_expected)
      message(FATAL_ERROR "${_irfq_name} SHA-256 mismatch: expected ${_irfq_expected}, got ${_irfq_actual}")
    endif()
  endforeach()
  unset(_irfq_actual)
  unset(_irfq_expected)
  unset(_irfq_fields)
  unset(_irfq_name)
  unset(_irfq_path)
  unset(_irfq_pin)
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

if (QUICKFIX_EXAMPLES)
  add_subdirectory(examples)
endif()

if (QUICKFIX_TESTS)
  if( WIN32)
    add_subdirectory(test)
  endif()
endif()

configure_file(cmake_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/src/C++/config.h @ONLY)
configure_file(cmake_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/include/quickfix/config.h @ONLY)
file(REMOVE config.h)


configure_file(cmake_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/include/quickfix/config.h @ONLY)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/spec/ DESTINATION share/quickfix FILES_MATCHING PATTERN "FIX*.xml")
