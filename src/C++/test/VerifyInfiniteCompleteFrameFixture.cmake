if(INFINITE_COMPLETE_FRAME_EXPECT_REJECTION)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
            -DINFINITE_COMPLETE_FRAME_FIXTURE_PATH=${INFINITE_COMPLETE_FRAME_FIXTURE_PATH}
            -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE verification_result
    ERROR_VARIABLE verification_error)
  if(verification_result EQUAL 0 OR
     NOT verification_error MATCHES "complete-frame fixture digest mismatch")
    message(FATAL_ERROR "tampered complete-frame fixture was not rejected")
  endif()
  return()
endif()

file(SHA256 "${INFINITE_COMPLETE_FRAME_FIXTURE_PATH}" infinite_complete_frame_fixture_sha256)
if(NOT infinite_complete_frame_fixture_sha256 STREQUAL
   "60d02ae5f56f7d4c9e8239eb435b1cebd2295c57ea86bd68d49820a9d620996b")
  message(FATAL_ERROR "complete-frame fixture digest mismatch")
endif()
