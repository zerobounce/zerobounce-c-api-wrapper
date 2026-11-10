cmake_minimum_required(VERSION 3.21)
project(ZeroBounce VERSION 2.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(WIN32)
  set(CURL_ENABLE_SSL ON)
  set(CURL_USE_SCHANNEL ON CACHE INTERNAL "" FORCE)
  set(CURL_WINDOWS_SSPI ON)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)

# Download and build json-c
FetchContent_Declare(
  json-c
  GIT_REPOSITORY https://github.com/json-c/json-c.git
  GIT_TAG json-c-0.17-20230812
)
# Download and build libcurl 8.23.0, pinned to the commit the curl-8_23_0 tag
# points at. NTLM stays off to avoid weak DES crypto in CodeQL/security scans:
# since curl 8.20 it is opt-in through CURL_ENABLE_NTLM.
FetchContent_Declare(
  libcurl
  GIT_REPOSITORY https://github.com/curl/curl.git
  GIT_TAG 3163ec065643ba20709f190111f42c3e1fe95273 # curl-8_23_0
)
set(CURL_ENABLE_NTLM OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
# The SDK only speaks HTTPS, so libcurl is built without the optional libraries
# it would otherwise pick up from whatever the build machine has installed.
# That keeps builds the same from machine to machine and stops, for example, a
# Homebrew libssh2 built against another OpenSSL from mixing two OpenSSL
# versions into one link. Compression is not requested (no
# CURLOPT_ACCEPT_ENCODING), and HTTP/2 was never enabled on purpose: curl 8.0.1
# left nghttp2 off by default, while 8.23 picks it up when it is installed.
# These are cache defaults rather than FORCE, so a -D on the command line or a
# parent project's own setting still wins.
set(CURL_ZLIB OFF CACHE STRING "Build curl with ZLIB support (AUTO, ON or OFF)")
set(CURL_BROTLI OFF CACHE STRING "Build curl with brotli support (AUTO, ON or OFF)")
set(CURL_ZSTD OFF CACHE STRING "Build curl with zstd support (AUTO, ON or OFF)")
set(USE_NGHTTP2 OFF CACHE BOOL "Use nghttp2 library")
set(USE_LIBIDN2 OFF CACHE BOOL "Use libidn2 for IDN support")
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "Use libssh2")
set(CURL_USE_LIBSSH OFF CACHE BOOL "Use libssh")
set(CURL_USE_GSSAPI OFF CACHE BOOL "Use GSSAPI")
# HTTP and HTTPS only: every other protocol curl can build is turned off.
foreach(_zb_curl_protocol DICT FILE FTP GOPHER IMAP IPFS LDAP LDAPS MQTT POP3 RTSP SMTP
        TELNET TFTP WEBSOCKETS)
  set(CURL_DISABLE_${_zb_curl_protocol} ON CACHE BOOL "Disable ${_zb_curl_protocol} in the bundled libcurl")
endforeach()
# TLS backend: Schannel on Windows (set above), OpenSSL everywhere else.
# curl 8.15 removed Secure Transport, so macOS builds against OpenSSL too
# (e.g. Homebrew's openssl@3, located with -DOPENSSL_ROOT_DIR=...). Certificates
# are still checked against the macOS trust store, through Apple SecTrust.
# This is a default rather than FORCE, so -DUSE_APPLE_SECTRUST=OFF still works.
if(APPLE)
  set(USE_APPLE_SECTRUST ON CACHE BOOL "Use Apple OS-native certificate verification")
endif()
# Download and build unity
FetchContent_Declare(
  unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG        v2.5.2
)
# Download and build fff
FetchContent_Declare(
  fff
  GIT_REPOSITORY https://github.com/meekrosoft/fff.git
  GIT_TAG        v1.1
)

FetchContent_MakeAvailable(json-c libcurl unity fff)

file(GLOB_RECURSE SOURCES "src/*.c")
add_library(${PROJECT_NAME} SHARED ${SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC include ${json-c_SOURCE_DIR})

target_link_libraries(${PROJECT_NAME} PUBLIC json-c libcurl)

enable_testing()
add_executable(ZeroBounceTests test/ZeroBounceTest.c test/OfflineApiTest.c test/OfflineWarmupTest.c)
target_link_libraries(ZeroBounceTests PRIVATE ${PROJECT_NAME} unity)
target_include_directories(ZeroBounceTests PRIVATE ${fff_SOURCE_DIR})

# The live suite calls the real API and spends credits, so it is opt-in and
# never built by a plain `cmake --build build`:
#   cmake -S . -B build-live -DZEROBOUNCE_BUILD_LIVE_TESTS=ON
#   cmake --build build-live --target ZeroBounceLiveApi
option(ZEROBOUNCE_BUILD_LIVE_TESTS "Build the opt-in live API suite" OFF)
if(ZEROBOUNCE_BUILD_LIVE_TESTS)
  add_executable(ZeroBounceLiveApi live/live_api.c)
  target_link_libraries(ZeroBounceLiveApi PRIVATE ${PROJECT_NAME})
endif()
