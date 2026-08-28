add_subdirectory(test)

set(quickfix_VERSION_MAJOR 17)
set(quickfix_VERSION_MINOR 0)
set(quickfix_VERSION_PATCH 0)
set(quickfix_VERSION ${quickfix_VERSION_MAJOR}.${quickfix_VERSION_MINOR}.${quickfix_VERSION_PATCH} )

set(quickfix_SOURCES
  Acceptor.cpp
  DataDictionary.cpp
  DataDictionaryProvider.cpp
  Dictionary.cpp
  FieldConvertors.cpp
  FieldMap.cpp
  FieldTypes.cpp
  FileLog.cpp
  FileStore.cpp
  Group.cpp
  HostDetailsProvider.cpp
  HttpConnection.cpp
  HttpMessage.cpp
  HttpParser.cpp
  HttpServer.cpp
  InfiniteCompleteFrame.cpp
  InfiniteFrameAdapter.cpp
  InfiniteSessionClassification.cpp
  Initiator.cpp
  Log.cpp
  Message.cpp
  MessageSorters.cpp
  MessageStore.cpp
  MySQLLog.cpp
  MySQLStore.cpp
  NullStore.cpp
  OdbcLog.cpp
  OdbcStore.cpp
  Parser.cpp
  PostgreSQLLog.cpp
  PostgreSQLStore.cpp
  pugixml.cpp
  PUGIXML_DOMDocument.cpp
  Session.cpp
  SessionFactory.cpp
  SessionSettings.cpp
  Settings.cpp
  SocketAcceptor.cpp
  SocketConnection.cpp
  SocketConnector.cpp
  SocketInitiator.cpp
  SocketServer.cpp
  stdafx.cpp
  ThreadedSocketAcceptor.cpp
  ThreadedSocketConnection.cpp
  ThreadedSocketInitiator.cpp
  TimeRange.cpp
  Utility.cpp
)

if(IRFQ_INFINITE_EMBED_DICTIONARIES)
  set(_irfq_embedded_dir "${CMAKE_CURRENT_BINARY_DIR}/infinite-embedded")
  set(_irfq_embedded_table "${_irfq_embedded_dir}/table.o")
  set(_irfq_embedded_unlocalized "${_irfq_embedded_dir}/dictionaries.unlocalized.o")
  set(_irfq_embedded_bundle "${_irfq_embedded_dir}/dictionaries.o")
  set(_irfq_embedded_commands)
  set(_irfq_embedded_dependencies "${PROJECT_SOURCE_DIR}/src/C++/InfiniteEmbeddedDictionaries.S")
  set(_irfq_embedded_objects)
  set(_irfq_localize_args)
  foreach(_irfq_name IN LISTS IRFQ_INFINITE_DICTIONARY_NAMES)
    string(REPLACE "." "_" _irfq_symbol "${_irfq_name}")
    set(_irfq_input "${PROJECT_SOURCE_DIR}/spec/${_irfq_name}")
    set(_irfq_object "${_irfq_embedded_dir}/${_irfq_symbol}.o")
    list(APPEND _irfq_embedded_dependencies "${_irfq_input}")
    list(APPEND _irfq_embedded_objects "${_irfq_object}")
    list(APPEND _irfq_embedded_commands
         COMMAND "${IRFQ_INFINITE_OBJCOPY}"
                 -I binary
                 -O elf64-x86-64
                 -B i386:x86-64
                 --rename-section .data=.rodata,alloc,load,readonly,data,contents
                 --add-section .note.GNU-stack=/dev/null
                 --set-section-flags .note.GNU-stack=noload,readonly
                 "${_irfq_name}"
                 "${_irfq_object}")
    foreach(_irfq_boundary IN ITEMS start end size)
      list(APPEND _irfq_localize_args "--localize-symbol=_binary_${_irfq_symbol}_${_irfq_boundary}")
    endforeach()
  endforeach()
  add_custom_command(
    OUTPUT "${_irfq_embedded_bundle}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_irfq_embedded_dir}"
    ${_irfq_embedded_commands}
    COMMAND "${CMAKE_C_COMPILER}"
            -c
            "${PROJECT_SOURCE_DIR}/src/C++/InfiniteEmbeddedDictionaries.S"
            -o "${_irfq_embedded_table}"
    COMMAND "${CMAKE_LINKER}"
            -r
            -o "${_irfq_embedded_unlocalized}"
            "${_irfq_embedded_table}"
            ${_irfq_embedded_objects}
    COMMAND "${IRFQ_INFINITE_OBJCOPY}"
            ${_irfq_localize_args}
            "${_irfq_embedded_unlocalized}"
            "${_irfq_embedded_bundle}"
    DEPENDS ${_irfq_embedded_dependencies}
    BYPRODUCTS "${_irfq_embedded_table}" "${_irfq_embedded_unlocalized}" ${_irfq_embedded_objects}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/spec"
    COMMENT "Embedding approved Infinite adapter dictionaries"
    VERBATIM)
  set_source_files_properties("${_irfq_embedded_bundle}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
  list(APPEND quickfix_SOURCES "${_irfq_embedded_bundle}")
endif()

include(CheckCSourceCompiles)
set(_quickfix_saved_required_includes "${CMAKE_REQUIRED_INCLUDES}")
set(CMAKE_REQUIRED_INCLUDES ${PROJECT_SOURCE_DIR}/src/C++)
check_c_source_compiles(
  "#include <InfiniteFrameAdapter.h>\nint main(void) { irfq_infinite_handle_v1 handle = {0, 0}; return (int)handle.object; }"
  INFINITE_FRAME_ADAPTER_HEADER_IS_C_COMPATIBLE)
set(CMAKE_REQUIRED_INCLUDES "${_quickfix_saved_required_includes}")
unset(_quickfix_saved_required_includes)
if(NOT INFINITE_FRAME_ADAPTER_HEADER_IS_C_COMPATIBLE)
  message(FATAL_ERROR "InfiniteFrameAdapter.h must compile as C")
endif()

if (WIN32)
  list(APPEND quickfix_SOURCES SocketMonitor_WIN32.cpp)
else()
  list(APPEND quickfix_SOURCES SocketMonitor_UNIX.cpp)
endif()

if (HAVE_SSL)
  set (quickfix_SOURCES ${quickfix_SOURCES}
    SSLSocketAcceptor.cpp
    SSLSocketConnection.cpp
    SSLSocketInitiator.cpp
    ThreadedSSLSocketAcceptor.cpp
    ThreadedSSLSocketConnection.cpp
    ThreadedSSLSocketInitiator.cpp
    UtilitySSL.cpp)
endif()

if (WIN32)
  add_library(${PROJECT_NAME} STATIC  ${quickfix_SOURCES})
  set_target_properties (${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX d)
  target_link_libraries(${PROJECT_NAME} ${OPENSSL_LIBRARIES} ${MYSQL_CLIENT_LIBS} ${PostgreSQL_LIBRARIES} ws2_32 crypt32)
else()
   if (QUICKFIX_SHARED_LIBS)
	   add_library(${PROJECT_NAME} SHARED  ${quickfix_SOURCES})
   else()
	   add_library(${PROJECT_NAME} STATIC  ${quickfix_SOURCES})
  endif()
  target_link_libraries(${PROJECT_NAME} ${OPENSSL_LIBRARIES} ${MYSQL_CLIENT_LIBS} ${PostgreSQL_LIBRARIES} ${ODBC_LIBRARIES} pthread)
endif()

include(CheckFunctionExists)
check_symbol_exists(ftime "sys/timeb.h" HAVE_FTIME)
if(HAVE_FTIME)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_FTIME)
endif()

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
if(IRFQ_INFINITE_EMBED_DICTIONARIES)
  target_compile_definitions(${PROJECT_NAME} PRIVATE IRFQ_INFINITE_EMBED_DICTIONARIES=1)
else()
  target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE IRFQ_INFINITE_DICTIONARY_PATH="${CMAKE_INSTALL_PREFIX}/share/quickfix")
endif()
target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/C++)

set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${quickfix_VERSION} SOVERSION ${quickfix_VERSION_MAJOR} )

install(TARGETS ${PROJECT_NAME} EXPORT quickfix-config DESTINATION lib)
install(EXPORT quickfix-config DESTINATION share/cmake/quickfix)

get_target_property(debug_postfix ${PROJECT_NAME} DEBUG_POSTFIX)
if(${debug_postfix} STREQUAL debug_postfix-NOTFOUND)
   set (debug_postfix "")
else()
   if(CMAKE_BUILD_TYPE)
       string(TOUPPER ${CMAKE_BUILD_TYPE} build_type)
       if(NOT ${build_type} STREQUAL DEBUG)
          set (debug_postfix "")
       endif()
   endif()
endif()
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}${debug_postfix}.pdb:${PROJECT_NAME} DESTINATION lib OPTIONAL)


install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/C++/ DESTINATION include/quickfix
        FILES_MATCHING PATTERN "*.h"
        PATTERN double-conversion EXCLUDE
        PATTERN "Infinite*.h" EXCLUDE
        PATTERN test EXCLUDE)
install(FILES ${PROJECT_SOURCE_DIR}/src/C++/InfiniteFrameAdapter.h DESTINATION include/quickfix)

if(IRFQ_INFINITE_EMBED_DICTIONARIES AND QUICKFIX_TESTS)
  foreach(_irfq_case IN ITEMS DEBUG NO_SSL SHARED NON_LINUX NON_AMD64 WRONG_COMPILER WRONG_OBJCOPY WRONG_LINKER DICTIONARY_DRIFT)
    string(TOLOWER "${_irfq_case}" _irfq_case_lower)
    add_test(
      NAME infinite_embedded_rejects_${_irfq_case_lower}
      COMMAND "${CMAKE_COMMAND}"
              "-DIRFQ_INFINITE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
              "-DIRFQ_INFINITE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/infinite-embedded-${_irfq_case_lower}"
              "-DIRFQ_INFINITE_TEST_CASE=${_irfq_case}"
              "-DIRFQ_INFINITE_GENERATOR=${CMAKE_GENERATOR}"
              "-DIRFQ_INFINITE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
              "-DIRFQ_INFINITE_REAL_OBJCOPY=${IRFQ_INFINITE_OBJCOPY}"
              "-DIRFQ_INFINITE_REAL_LINKER=${CMAKE_LINKER}"
              -P "${PROJECT_SOURCE_DIR}/cmake/VerifyInfiniteEmbeddedConfigure.cmake")
  endforeach()
  add_test(
    NAME infinite_embedded_symbols_are_hidden
    COMMAND "${CMAKE_COMMAND}"
            "-DIRFQ_INFINITE_ARCHIVE=$<TARGET_FILE:${PROJECT_NAME}>"
            "-DIRFQ_INFINITE_AR=${CMAKE_AR}"
            "-DIRFQ_INFINITE_READELF=${IRFQ_INFINITE_READELF}"
            -P "${PROJECT_SOURCE_DIR}/cmake/VerifyInfiniteEmbeddedSymbols.cmake")
endif()
