cmake_minimum_required(VERSION 3.27)
project(ReaJSFX VERSION 0.1.2 LANGUAGES C CXX RC)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "ReaJSFX requires Windows x64 and MSVC")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
include(cmake/dependencies.cmake)
execute_process(COMMAND "${Python3_EXECUTABLE}" scripts/patch-dependencies.py
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" COMMAND_ERROR_IS_FATAL ANY)

set(CLAP_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/upstream/clap" CACHE PATH "CLAP SDK")
set(VST3_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/upstream/vst3sdk" CACHE PATH "VST3 SDK")
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_COPY_AFTER_BUILD OFF CACHE BOOL "" FORCE)
add_subdirectory(upstream/clap-wrapper EXCLUDE_FROM_ALL)
include(cmake/runtime.cmake)
add_library(modern_reajs_impl STATIC src/plugin.cpp src/config.cpp)
target_include_directories(modern_reajs_impl PUBLIC src upstream/clap/include)
target_compile_definitions(modern_reajs_impl PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
  _CRT_SECURE_NO_WARNINGS REAJSFX_VERSION="${ReaJSFX_VERSION}")
target_compile_options(modern_reajs_impl PRIVATE /utf-8 /EHsc)
target_link_libraries(modern_reajs_impl PUBLIC jsfx_runtime comdlg32)
# Retain the existing protocol IDs across the display-name change.
make_clapfirst_plugins(TARGET_NAME modern_reajs IMPL_TARGET modern_reajs_impl
  OUTPUT_NAME "reajsfx" ENTRY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/entry.cpp"
  BUNDLE_IDENTIFIER "org.modernreajs.host" BUNDLE_VERSION "${ReaJSFX_VERSION}"
  PLUGIN_FORMATS CLAP VST3 COPY_AFTER_BUILD FALSE WINDOWS_FOLDER_VST3 FALSE)
foreach(t modern_reajs_clap modern_reajs_vst3)
  set_target_properties(${t} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/$<CONFIG>")
endforeach()
target_compile_definitions(modern_reajs_vst3-clap-wrapper-vst3-lib PRIVATE NOMINMAX)
target_include_directories(modern_reajs_vst3-clap-wrapper-vst3-lib PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/upstream/reaper-sdk/sdk")
target_include_directories(clap-wrapper-shared-detail PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
include(cmake/vst2.cmake)

install(FILES "$<TARGET_FILE:reajsfx_vst2>" DESTINATION VST2)
install(FILES "$<TARGET_FILE:modern_reajs_clap>" DESTINATION CLAP)
install(FILES "$<TARGET_FILE:modern_reajs_vst3>" DESTINATION VST3/reajsfx.vst3/Contents/x86_64-win)
install(FILES README.md README.zh-CN.md LICENSE THIRD_PARTY_NOTICES.txt DESTINATION .)
install(DIRECTORY docs/ DESTINATION docs)
install(FILES dependencies/vst2-vestige/COPYING DESTINATION licenses RENAME VeSTige-GPL-2.0.txt)
