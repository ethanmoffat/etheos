cmake_minimum_required(VERSION 3.5)

project(ixwebsocket-download NONE)

include(ExternalProject)
ExternalProject_Add(ixwebsocket
  GIT_REPOSITORY    https://github.com/machinezone/IXWebSocket.git
  GIT_TAG           v11.4.5
  SOURCE_DIR        "${CMAKE_CURRENT_BINARY_DIR}/ixwebsocket-src"
  BINARY_DIR        "${CMAKE_CURRENT_BINARY_DIR}/ixwebsocket-build"
  CONFIGURE_COMMAND ""
  BUILD_COMMAND     ""
  INSTALL_COMMAND   ""
  TEST_COMMAND      ""
)
