
# Download and unpack IXWebSocket at configure time
if (NOT EOSERV_OFFLINE)
  configure_file(${CMAKE_SOURCE_DIR}/cmake/ixwebsocketproj.cmake ixwebsocket-download/CMakeLists.txt)

  execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ixwebsocket-download )
  if(result)
    message(FATAL_ERROR "CMake step for IXWebSocket failed: ${result}")
  endif()

  execute_process(COMMAND ${CMAKE_COMMAND} --build .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ixwebsocket-download )
  if(result)
    message(FATAL_ERROR "Build step for IXWebSocket failed: ${result}")
  endif()
endif()

# Configure IXWebSocket options before adding subdirectory
set(USE_TLS OFF CACHE BOOL "" FORCE)
set(USE_OPEN_SSL OFF CACHE BOOL "" FORCE)
set(USE_MBED_TLS OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)

# Add IXWebSocket directly to our build
add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/ixwebsocket-src
                 ${CMAKE_CURRENT_BINARY_DIR}/ixwebsocket-build
                 EXCLUDE_FROM_ALL)
