cmake_minimum_required(VERSION 3.20)

if(NOT WIN32)
  foreach(_f CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS)
    set(${_f} "${${_f}} -m32")
  endforeach()
endif()

project(amxxrpc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(amxxrpc SHARED
  src/main.cpp
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
  third_party/hlsdk/public)

target_compile_definitions(amxxrpc PRIVATE HAVE_STDINT_H NOMINMAX)

if(MSVC)
  set_target_properties(amxxrpc PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
  target_link_libraries(amxxrpc PRIVATE ws2_32)
  target_link_options(amxxrpc PRIVATE
    "/EXPORT:GiveFnptrsToDll=_GiveFnptrsToDll@8,@1"
    "/SECTION:.data,RW")
  set_target_properties(amxxrpc PROPERTIES OUTPUT_NAME amxxrpc_amxx)
else()
  target_compile_options(amxxrpc PRIVATE -ffunction-sections -fdata-sections -fcf-protection=none)
  target_link_libraries(amxxrpc PRIVATE dl m pthread)
  target_link_options(amxxrpc PRIVATE -static-libstdc++ -static-libgcc -Wl,--gc-sections)
  set_target_properties(amxxrpc PROPERTIES OUTPUT_NAME amxxrpc_amxx_i386)
endif()

set_target_properties(amxxrpc PROPERTIES PREFIX "")
