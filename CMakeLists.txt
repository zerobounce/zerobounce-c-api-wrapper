cmake_minimum_required(VERSION 3.20)
project(ZeroBounce VERSION 1.1.0 LANGUAGES C)

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
# Download and build libcurl
FetchContent_Declare(
  libcurl
  GIT_REPOSITORY https://github.com/curl/curl.git
  GIT_TAG curl-8_0_1
)
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
add_executable(ZeroBounceTests test/ZeroBounceTest.c)
target_link_libraries(ZeroBounceTests PRIVATE ${PROJECT_NAME} unity)
target_include_directories(ZeroBounceTests PRIVATE ${fff_SOURCE_DIR})
