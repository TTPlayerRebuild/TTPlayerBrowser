# Pin the existing SDK. A clean runner needs neither NuGet nor a user-profile cache.
set(TTPBROWSER_WEBVIEW2_SDK "" CACHE PATH "Use an already extracted Microsoft.Web.WebView2 1.0.3179.45 SDK")
if(NOT TTPBROWSER_WEBVIEW2_SDK)
  include(FetchContent)
  FetchContent_Declare(ttpbrowser_webview2
    URL https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.3179.45/microsoft.web.webview2.1.0.3179.45.nupkg
    URL_HASH SHA256=70bd381c5d67f97a6a80294001cde125ab5b24b6d36098ad901d8e15efd752d0
    DOWNLOAD_NAME webview2-1.0.3179.45.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(ttpbrowser_webview2)
  set(TTPBROWSER_WEBVIEW2_SDK "${ttpbrowser_webview2_SOURCE_DIR}" CACHE PATH "Extracted WebView2 SDK" FORCE)
endif()
foreach(_file build/native/include/WebView2.h build/native/x86/WebView2LoaderStatic.lib LICENSE.txt NOTICE.txt)
  if(NOT EXISTS "${TTPBROWSER_WEBVIEW2_SDK}/${_file}")
    message(FATAL_ERROR "Incomplete WebView2 SDK: ${TTPBROWSER_WEBVIEW2_SDK}/${_file}")
  endif()
endforeach()
