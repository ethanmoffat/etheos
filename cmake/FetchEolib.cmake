include(FetchContent)

# EOSERV_OFFLINE skips the download; eolib must already be populated in this build directory by a previous online
# configure (same behavior as googletest/bcrypt).
set(FETCHCONTENT_FULLY_DISCONNECTED ${EOSERV_OFFLINE} CACHE BOOL "" FORCE)
set(EOLIB_OFFLINE ${EOSERV_OFFLINE} CACHE BOOL "" FORCE)

FetchContent_Declare(
	eolib
	URL https://github.com/ethanmoffat/eolib-cpp/releases/download/v0.1.0-beta.7/eolib-0.1.0-beta.7-src.tar.gz
	URL_HASH SHA512=c2eeaae810933d000e184739af045e408202d001d3e9201c00e949999c1474b945c6b2bd96353a0542729e7384bf35e0a109250ab66aed56d04ce62f1f81f0a6)

if(EOSERV_OFFLINE AND NOT EXISTS "${FETCHCONTENT_BASE_DIR}/eolib-src/CMakeLists.txt")
	message(FATAL_ERROR "EOSERV_OFFLINE is ON, but eolib has not been downloaded to ${FETCHCONTENT_BASE_DIR}/eolib-src. Configure this build directory once without EOSERV_OFFLINE.")
endif()

FetchContent_MakeAvailable(eolib)
