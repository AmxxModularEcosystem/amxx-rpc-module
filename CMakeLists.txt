cmake_minimum_required(VERSION 3.20)

if(NOT WIN32)
  foreach(_f CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS)
    set(${_f} "${${_f}} -m32")
  endforeach()
endif()

project(amxxrpc C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(amxxrpc SHARED
  src/main.cpp
  src/Config.cpp
  src/Log.cpp
  src/Protocol.cpp
  src/Transport.cpp
  src/RpcDispatch.cpp
  src/RpcRegistry.cpp
  src/CoreMethods.cpp
  src/Events.cpp
  src/PawnApi.cpp
  src/Fake.cpp
  src/FakeMethods.cpp
  third_party/parson/parson.c
  third_party/amxx/sdk/amxxmodule.cpp)

target_include_directories(amxxrpc PRIVATE
  src
  third_party/amxx
  third_party/amxx/sdk
  third_party/metamod
  third_party/hlsdk/common
  third_party/hlsdk/dlls
  third_party/hlsdk/engine
  third_party/hlsdk/game_shared
  third_party/hlsdk/public
  third_party/parson)

target_compile_definitions(amxxrpc PRIVATE HAVE_STDINT_H NOMINMAX)

if(MSVC)
  set_target_properties(amxxrpc PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
  target_link_libraries(amxxrpc PRIVATE ws2_32 Threads::Threads)
  target_link_options(amxxrpc PRIVATE
    "/EXPORT:GiveFnptrsToDll=_GiveFnptrsToDll@8,@1"
    "/SECTION:.data,RW")
  set_target_properties(amxxrpc PROPERTIES OUTPUT_NAME amxxrpc_amxx)
else()
  target_compile_options(amxxrpc PRIVATE -ffunction-sections -fdata-sections -fcf-protection=none)
  target_link_libraries(amxxrpc PRIVATE dl m pthread Threads::Threads)
  target_link_options(amxxrpc PRIVATE -static-libstdc++ -static-libgcc -Wl,--gc-sections)
  if(WIN32)
    target_link_options(amxxrpc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/amxxrpc.def")
    set_target_properties(amxxrpc PROPERTIES OUTPUT_NAME amxxrpc_amxx)
  else()
    set_target_properties(amxxrpc PROPERTIES OUTPUT_NAME amxxrpc_amxx_i386)
  endif()
endif()

set_target_properties(amxxrpc PROPERTIES PREFIX "")

# Off-line protocol unit test (no AMXX SDK).
add_executable(protocol_test
  tests/protocol_test.cpp
  src/Protocol.cpp
  third_party/parson/parson.c)
target_include_directories(protocol_test PRIVATE src third_party/parson)
target_compile_definitions(protocol_test PRIVATE HAVE_STDINT_H NOMINMAX)
target_link_libraries(protocol_test PRIVATE Threads::Threads)

# Off-line registry unit test (no AMXX SDK).
add_executable(registry_test
  tests/registry_test.cpp
  src/RpcRegistry.cpp
  third_party/parson/parson.c)
target_include_directories(registry_test PRIVATE src third_party/parson)
target_compile_definitions(registry_test PRIVATE HAVE_STDINT_H NOMINMAX)
target_link_libraries(registry_test PRIVATE Threads::Threads)
