include(FetchContent)

# EOSERV_OFFLINE skips the download; eolib must already be populated in this build directory by a previous online
# configure (same behavior as googletest/bcrypt).
set(FETCHCONTENT_FULLY_DISCONNECTED ${EOSERV_OFFLINE} CACHE BOOL "" FORCE)
set(EOLIB_OFFLINE ${EOSERV_OFFLINE} CACHE BOOL "" FORCE)

FetchContent_Declare(
	eolib
	URL https://github.com/ethanmoffat/eolib-cpp/releases/download/v0.1.0-beta.5/eolib-0.1.0-beta.5-src.tar.gz
	URL_HASH SHA512=abe487103d6016b6e20a057593a2c41e47e8d32092d18dcca93f0f89fb1f2dfb155af56c7b6e220a3c6776272124f178441884402255aa4885ae651c45b9ca6d)

if(EOSERV_OFFLINE AND NOT EXISTS "${FETCHCONTENT_BASE_DIR}/eolib-src/CMakeLists.txt")
	message(FATAL_ERROR "EOSERV_OFFLINE is ON, but eolib has not been downloaded to ${FETCHCONTENT_BASE_DIR}/eolib-src. Configure this build directory once without EOSERV_OFFLINE.")
endif()

FetchContent_MakeAvailable(eolib)
