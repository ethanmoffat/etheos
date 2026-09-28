include(FetchContent)

# EOSERV_OFFLINE skips the download; eolib must already be populated in this build directory by a previous online
# configure (same behavior as googletest/bcrypt).
set(FETCHCONTENT_FULLY_DISCONNECTED ${EOSERV_OFFLINE} CACHE BOOL "" FORCE)
set(EOLIB_OFFLINE ${EOSERV_OFFLINE} CACHE BOOL "" FORCE)

FetchContent_Declare(
	eolib
	URL https://github.com/ethanmoffat/eolib-cpp/releases/download/v0.1.0-beta.3/eolib-0.1.0-beta.3-src.tar.gz
	URL_HASH SHA512=6ad7603af3573503ac01a6936d86c99af67056afb815e34d1daa3654117aceb82a05535e459d8af6fd85f18a8e007bf5964735cd4464087743b255071523fbc6)

if(EOSERV_OFFLINE AND NOT EXISTS "${FETCHCONTENT_BASE_DIR}/eolib-src/CMakeLists.txt")
	message(FATAL_ERROR "EOSERV_OFFLINE is ON, but eolib has not been downloaded to ${FETCHCONTENT_BASE_DIR}/eolib-src. Configure this build directory once without EOSERV_OFFLINE.")
endif()

FetchContent_MakeAvailable(eolib)
