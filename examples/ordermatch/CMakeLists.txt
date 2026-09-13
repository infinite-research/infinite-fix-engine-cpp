if (HAVE_SSL AND WIN32)
set(applink_SOURCE ${OPENSSL_INCLUDE_DIR}/openssl/applink.c)
endif()

add_executable(ordermatch Application.cpp Market.cpp ordermatch.cpp ${applink_SOURCE})

target_include_directories(ordermatch PRIVATE ${PROJECT_BINARY_DIR}/include ${PROJECT_SOURCE_DIR})


target_link_libraries(ordermatch ${PROJECT_NAME})

if (QUICKFIX_TESTS)
  add_executable(ordermatch_tests
                 Application.cpp
                 Market.cpp
                 OrderMatcherTestCase.cpp
                 ${PROJECT_SOURCE_DIR}/src/C++/test/catch_amalgamated.cpp
                 ${applink_SOURCE})

  target_include_directories(ordermatch_tests PRIVATE
                             ${PROJECT_BINARY_DIR}/include
                             ${PROJECT_SOURCE_DIR}
                             ${PROJECT_SOURCE_DIR}/src/C++/test)

  target_link_libraries(ordermatch_tests ${PROJECT_NAME})

  add_test(NAME ordermatch_tests COMMAND ordermatch_tests)
endif()

if (NOT WIN32)
ADD_CUSTOM_TARGET(ordermatch_target ALL
                  COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/bin
                  COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:ordermatch> ${PROJECT_BINARY_DIR}/bin/ordermatch)
else()
set_target_properties(ordermatch PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PROJECT_BINARY_DIR}/bin/debug/ordermatch/
                      RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PROJECT_BINARY_DIR}/bin/release/ordermatch/
                      RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${PROJECT_BINARY_DIR}/bin/release/ordermatch/)
endif()
