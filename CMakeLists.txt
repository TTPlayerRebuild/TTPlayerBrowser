cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(TTPBrowser VERSION 1.0.0 LANGUAGES C CXX RC)
  set(TTPBROWSER_STANDALONE ON)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CONFIGURATION_TYPES Release CACHE STRING "Configuration" FORCE)
  include(cmake/xp_runtime.cmake)
  include(cmake/wtl.cmake)
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "ttpbrowser requires MSVC Win32/x86")
endif()
if(TTPBROWSER_STANDALONE)
  set(_rebuild "${CMAKE_CURRENT_SOURCE_DIR}/support/rebuild")
  include(cmake/check_shared_sources.cmake)
else()
  # The host keeps its authoritative shared implementation in the parent tree.
  set(_rebuild "${CMAKE_CURRENT_SOURCE_DIR}/../rebuild")
endif()
include(cmake/webview2.cmake)
include(cmake/version.cmake)
include(cmake/size_release.cmake)
# Remove the obsolete cache entry when upgrading an existing build directory.
unset(TTPBROWSER_WEBVIEW2_LOADER CACHE)
add_library(ttpbrowser SHARED
  src/module.cpp src/ttpbrowser.def "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
  src/browser_window.cpp src/browser_bridge.cpp
  src/webview_backend.cpp src/ie_backend.cpp
  src/downloads.cpp src/download_skin.cpp src/http_download_queue.cpp
  "${_rebuild}/src/audio/legacy_cache.cpp"
  "${_rebuild}/src/core/text.cpp"
  "${_rebuild}/src/skin/skin_package.cpp"
)
add_dependencies(ttpbrowser ttpbrowser_version)
ttpbrowser_optimize_size(ttpbrowser)
target_include_directories(ttpbrowser PUBLIC include PRIVATE src "${_rebuild}/include"
  "${_rebuild}/third_party/ttpcomm-sdk/include"
  "${TTPBROWSER_WEBVIEW2_SDK}/build/native/include")
target_compile_features(ttpbrowser PRIVATE cxx_std_20)
target_compile_definitions(ttpbrowser PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(ttpbrowser PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(ttpbrowser PRIVATE ttplayer_wtl_headers ole32 oleaut32 comsuppw uuid
  user32 gdi32 comctl32 comdlg32 shell32 shlwapi wininet msimg32 advapi32 version
  "${TTPBROWSER_WEBVIEW2_SDK}/build/native/x86/WebView2LoaderStatic.lib")
set_target_properties(ttpbrowser PROPERTIES OUTPUT_NAME ttpbrowser)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(TARGET ttpbrowser POST_BUILD
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_legacy_imports.py"
    "$<TARGET_FILE:ttpbrowser>"
    --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/5.1.2600.txt"
    --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/6.1.7600.txt"
    --report "$<TARGET_FILE_DIR:ttpbrowser>/legacy-imports.json" VERBATIM)
install(TARGETS ttpbrowser RUNTIME DESTINATION . COMPONENT Runtime)
option(TTPBROWSER_BUILD_TESTS "Build music window DLL contract tests" OFF)
if(TTPBROWSER_BUILD_TESTS)
  enable_testing()
  add_executable(ttpbrowser_contract_tests tests/contract_tests.cpp)
  target_include_directories(ttpbrowser_contract_tests PRIVATE include)
  target_compile_features(ttpbrowser_contract_tests PRIVATE cxx_std_20)
  target_compile_definitions(ttpbrowser_contract_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(ttpbrowser_contract_tests PRIVATE /W4 /EHsc /utf-8)
  target_link_libraries(ttpbrowser_contract_tests PRIVATE ole32 oleaut32 uuid user32 shell32)
  add_dependencies(ttpbrowser_contract_tests ttpbrowser)
  add_test(NAME ttpbrowser_contract_tests COMMAND ttpbrowser_contract_tests "$<TARGET_FILE:ttpbrowser>")
  add_executable(ttpbrowser_runtime_tests tests/runtime_tests.cpp)
  target_include_directories(ttpbrowser_runtime_tests PRIVATE include)
  target_compile_definitions(ttpbrowser_runtime_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(ttpbrowser_runtime_tests PRIVATE /W4 /EHsc /utf-8)
  target_link_libraries(ttpbrowser_runtime_tests PRIVATE ole32 oleaut32 uuid user32 gdi32)
  add_dependencies(ttpbrowser_runtime_tests ttpbrowser)
  add_executable(ttpbrowser_download_tests tests/downloads_tests.cpp src/downloads.cpp src/download_skin.cpp
    src/http_download_queue.cpp "${_rebuild}/src/audio/legacy_cache.cpp" "${_rebuild}/src/core/text.cpp"
    "${_rebuild}/src/skin/skin_package.cpp")
  target_include_directories(ttpbrowser_download_tests PRIVATE include src "${_rebuild}/include"
    "${_rebuild}/third_party/ttpcomm-sdk/include")
  target_compile_definitions(ttpbrowser_download_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(ttpbrowser_download_tests PRIVATE /W4 /EHsc /utf-8)
  target_link_libraries(ttpbrowser_download_tests PRIVATE ole32 oleaut32 comsuppw advapi32 comctl32 comdlg32 user32 gdi32 shell32 uuid msimg32)
  add_test(NAME ttpbrowser_download_tests COMMAND ttpbrowser_download_tests)
  # Exercise the same themed common-controls implementation as the real EXE.
  # Without its v6 activation context, tooltip write-buffer bugs can be missed.
  foreach(browser_test ttpbrowser_contract_tests ttpbrowser_runtime_tests ttpbrowser_download_tests)
    target_sources(${browser_test} PRIVATE "${_rebuild}/src/app/ttplayer.manifest")
  endforeach()
  target_link_libraries(ttpbrowser_contract_tests PRIVATE comctl32)
endif()
