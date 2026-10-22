set(TTPBROWSER_BUILD_VERSION "" CACHE STRING "Beijing yyyy.MM.dd[pN] version; empty selects the current date")
find_program(TTPBROWSER_POWERSHELL NAMES powershell pwsh REQUIRED)
set(_browser_version_args)
if(NOT TTPBROWSER_BUILD_VERSION STREQUAL "")
  list(APPEND _browser_version_args -Version "${TTPBROWSER_BUILD_VERSION}")
endif()
add_custom_target(ttpbrowser_version
  COMMAND "${TTPBROWSER_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/cmake/write_version.ps1"
    -Template "${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in"
    -OutputPath "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
    ${_browser_version_args}
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
  COMMENT "Updating Browser file and product versions"
  VERBATIM)
