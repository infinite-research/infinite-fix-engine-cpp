add_definitions(-DCATCH_AMALGAMATED_CUSTOM_MAIN)

if (QUICKFIX_TESTS)
  set(infinite_complete_frame_fixture
      ${PROJECT_SOURCE_DIR}/test/fixtures/complete-frame-gate.v1.tsv)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               ${infinite_complete_frame_fixture})
  set(INFINITE_COMPLETE_FRAME_FIXTURE_PATH ${infinite_complete_frame_fixture})
  include(${CMAKE_CURRENT_LIST_DIR}/VerifyInfiniteCompleteFrameFixture.cmake)

  configure_file(${infinite_complete_frame_fixture}
                 ${CMAKE_CURRENT_BINARY_DIR}/tampered-complete-frame-gate.v1.tsv
                 COPYONLY)
  file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/tampered-complete-frame-gate.v1.tsv "\n")
  add_test(
    NAME infinite_complete_frame_fixture_rejects_tampering
    COMMAND ${CMAKE_COMMAND}
            -DINFINITE_COMPLETE_FRAME_FIXTURE_PATH=${CMAKE_CURRENT_BINARY_DIR}/tampered-complete-frame-gate.v1.tsv
            -DINFINITE_COMPLETE_FRAME_EXPECT_REJECTION=ON
            -P ${CMAKE_CURRENT_LIST_DIR}/VerifyInfiniteCompleteFrameFixture.cmake)

  set (ut_SOURCES 
  ${PROJECT_SOURCE_DIR}/src/C++/test/catch_amalgamated.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/DataDictionaryTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/DataDictionaryTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/DictionaryTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FieldBaseTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FieldConvertorsTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FieldMapTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FieldTypesTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FileLogTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FileStoreFactoryTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FileStoreTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/FileUtilitiesTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/GroupTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/HostDetailsProviderTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/HttpMessageTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/HttpParserTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/InfiniteCompleteFrameTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/InfiniteSessionClassificationTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/MemoryStoreTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/MessageSortersTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/MessagesTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/MySQLStoreTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/NullStoreTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/OdbcStoreTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/ParserTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/PostgreSQLStoreTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SessionFactoryTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SessionIDTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SessionSettingsTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SessionStateTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SessionTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SettingsTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SharedArrayTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SocketAcceptorTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SocketConnectionTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SocketConnectorTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SocketMonitorTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/SocketServerTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/StringUtilitiesTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/TestHelper.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/TimeRangeTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/UtcTimeOnlyTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/UtcTimeStampTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/UtilityTestCase.cpp
  ${PROJECT_SOURCE_DIR}/src/C++/test/UtilitySSLTestCase.cpp)

  add_executable(ut ut.cpp ${ut_SOURCES})
  target_compile_definitions(ut PRIVATE
    INFINITE_COMPLETE_FRAME_FIXTURE_PATH="${infinite_complete_frame_fixture}")
  
  target_include_directories(ut PRIVATE ${PROJECT_SOURCE_DIR} 
    ${PROJECT_SOURCE_DIR}/src/C++)
    
  target_link_libraries(ut ${PROJECT_NAME})

  add_test(
    NAME infinite_complete_frame_dispatcher
    COMMAND ut "InfiniteCompleteFrameDispatcherTests"
               --quickfix-spec-path ${PROJECT_SOURCE_DIR}/spec)
  add_test(
    NAME quickfix_unit
    COMMAND ut --quickfix-spec-path ${PROJECT_SOURCE_DIR}/spec)
  
  if (NOT WIN32)
  ADD_CUSTOM_TARGET(ut_target ALL
                    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:ut> ${PROJECT_SOURCE_DIR}/test/ut)
  else()
  set_target_properties(ut PROPERTIES
                        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PROJECT_SOURCE_DIR}/test/debug/ut/
                        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PROJECT_SOURCE_DIR}/test/release/ut/
                        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${PROJECT_SOURCE_DIR}/test/release/ut/)
  endif()
endif()
