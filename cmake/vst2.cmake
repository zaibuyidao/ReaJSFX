set(vst2_tree "${CMAKE_CURRENT_BINARY_DIR}/official-vst2")
execute_process(COMMAND "${Python3_EXECUTABLE}" scripts/prepare-vst2.py --output "${vst2_tree}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" COMMAND_ERROR_IS_FATAL ANY)
set(vst2_project "${vst2_tree}/jsfx/reajs-vst2")
file(STRINGS "${vst2_project}/reajs-vst2.vcxproj" source_lines REGEX "<ClCompile Include=")
set(vst2_sources)
foreach(line IN LISTS source_lines)
  string(REGEX MATCH "Include=\"([^\"]+)\"" match "${line}")
  string(REPLACE "\\" "/" relative "${CMAKE_MATCH_1}")
  list(APPEND vst2_sources "${vst2_project}/${relative}")
endforeach()
add_library(reajsfx_vst2 MODULE ${vst2_sources}
  "${vst2_project}/res.rc" "${vst2_tree}/WDL/eel2/asm-nseel-x64.obj")
math(EXPR vst2_version "${ReaJSFX_VERSION_MAJOR} * 10000 + ${ReaJSFX_VERSION_MINOR} * 100 + ${ReaJSFX_VERSION_PATCH}")
target_compile_definitions(reajsfx_vst2 PRIVATE WIN32 _WINDOWS _USRDLL BUILD_WINDOWS
  _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS WDL_FFT_REALSIZE=8 _SUPPORT_GFX_VIS_ JS_NO_TEXTUI
  REAPER_FXEMBED_WM_NOBORDER=0x100001 REAPER_FXEMBED_WM_HITTEST=0x100002
  REAJSFX_VST2_VERSION=${vst2_version})
target_compile_options(reajsfx_vst2 PRIVATE
  "$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8;/wd4244;/wd4267;/wd4018;/wd4800;/wd4996>")
target_include_directories(reajsfx_vst2 PRIVATE "${vst2_project}")
target_link_libraries(reajsfx_vst2 PRIVATE comctl32 user32 gdi32 comdlg32 shell32 winmm ole32 msimg32 advapi32)
set_target_properties(reajsfx_vst2 PROPERTIES PREFIX "" OUTPUT_NAME ReaJSFX SUFFIX .dll
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/$<CONFIG>"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/$<CONFIG>")
