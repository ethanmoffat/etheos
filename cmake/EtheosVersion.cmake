# Computes ETHEOS_VERSION_FULL (the numeric project version plus any pre-release suffix) and generates version.h.
# Use scripts/prepare-release.sh to change the version instead of editing CMakeLists.txt by hand.

if(ETHEOS_VERSION_SUFFIX)
	if(NOT ETHEOS_VERSION_SUFFIX MATCHES "^rc\\.[1-9][0-9]*$")
		message(FATAL_ERROR "ETHEOS_VERSION_SUFFIX '${ETHEOS_VERSION_SUFFIX}' must be of the form rc.N")
	endif()
	set(ETHEOS_VERSION_FULL "${PROJECT_VERSION}-${ETHEOS_VERSION_SUFFIX}")
else()
	set(ETHEOS_VERSION_FULL "${PROJECT_VERSION}")
endif()

message(STATUS "etheos version: ${ETHEOS_VERSION_FULL}")

configure_file(${CMAKE_SOURCE_DIR}/cmake/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)
