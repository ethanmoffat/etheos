cmake_minimum_required(VERSION 3.5)
cmake_policy(SET CMP0054 NEW)

project(bcrypt-download NONE)

include(ExternalProject)
ExternalProject_Add(bcrypt
  GIT_REPOSITORY    https://github.com/trusch/libbcrypt.git
  GIT_TAG           master
  SOURCE_DIR        "${CMAKE_CURRENT_BINARY_DIR}/bcrypt-src"
  BINARY_DIR        "${CMAKE_CURRENT_BINARY_DIR}/bcrypt-build"
  CONFIGURE_COMMAND ""
  BUILD_COMMAND     ""
  INSTALL_COMMAND   ""
  TEST_COMMAND      ""
)