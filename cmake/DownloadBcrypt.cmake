
# Download and unpack bcrypt at configure time
if (NOT EOSERV_OFFLINE)
  configure_file(${CMAKE_SOURCE_DIR}/cmake/bcryptproj.cmake bcrypt-download/CMakeLists.txt)

  execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bcrypt-download )
  if(result)
    message(FATAL_ERROR "CMake step for bcrypt failed: ${result}")
  endif()

  execute_process(COMMAND ${CMAKE_COMMAND} --build .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bcrypt-download )
  if(result)
    message(FATAL_ERROR "Build step for bcrypt failed: ${result}")
  endif()
endif()

# Add bcrypt directly to our build. This defines
# the gtest and bcrypt targets.
add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/bcrypt-src
                 ${CMAKE_CURRENT_BINARY_DIR}/bcrypt-build
                 EXCLUDE_FROM_ALL)
