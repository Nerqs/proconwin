cmake_minimum_required(VERSION 3.20)
project(procon_proto LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# hidapi: baixado automaticamente na primeira compilação.
include(FetchContent)
set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  hidapi
  GIT_REPOSITORY https://github.com/libusb/hidapi.git
  GIT_TAG        hidapi-0.14.0
)
FetchContent_MakeAvailable(hidapi)

add_executable(procon_proto
  src/main.cpp
  src/protocol.cpp
  src/controller.cpp
)

if(MSVC)
  target_compile_options(procon_proto PRIVATE /W4 /utf-8)
else()
  target_compile_options(procon_proto PRIVATE -Wall -Wextra)
endif()

target_link_libraries(procon_proto PRIVATE hidapi::hidapi)
