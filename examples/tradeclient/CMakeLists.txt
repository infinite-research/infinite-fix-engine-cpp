if (HAVE_SSL AND WIN32)
set(applink_SOURCE ${OPENSSL_INCLUDE_DIR}/openssl/applink.c)
endif()

add_executable(tradeclient Application.cpp tradeclient.cpp ${applink_SOURCE})

target_include_directories(tradeclient PRIVATE ${PROJECT_BINARY_DIR}/include ${PROJECT_SOURCE_DIR})


target_link_libraries(tradeclient ${PROJECT_NAME})

if (NOT WIN32)
ADD_CUSTOM_TARGET(tradeclient_target ALL
                  COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/bin
                  COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:tradeclient> ${PROJECT_BINARY_DIR}/bin/tradeclient)
else()
set_target_properties(tradeclient PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PROJECT_BINARY_DIR}/bin/debug/tradeclient/
                      RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PROJECT_BINARY_DIR}/bin/release/tradeclient/
                      RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${PROJECT_BINARY_DIR}/bin/release/tradeclient/)
endif()    
