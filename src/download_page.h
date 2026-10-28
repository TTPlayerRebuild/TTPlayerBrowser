#pragma once
#include "ttpbrowser.h"
#include <string>
namespace ttpbrowser {
constexpr UINT kOpenDownloadPage=WM_APP+0x323;
struct DownloadPage {std::wstring name,url;};
inline DownloadPage GetDownloadPage(const ttp_browser_host& host) {
    DownloadPage page;
    if(!host.get_download_page||!host.recognize_page_download)return page;
    BSTR name{},url{};const auto hr=host.get_download_page(host.context,&name,&url);
    if(hr==S_OK&&name&&url){page.name.assign(name,SysStringLen(name));page.url.assign(url,SysStringLen(url));}
    SysFreeString(name);SysFreeString(url);return page;
}
inline void OpenDownloadPage(HWND child) {
    // MediaSurface and its optional native view are nested child windows.
    HWND parent=GetParent(child);wchar_t type[80]{};
    while(parent){GetClassNameW(parent,type,80);if(!wcscmp(type,L"TTPlayer_BrowserWnd")){PostMessageW(parent,kOpenDownloadPage,0,0);return;}parent=GetParent(parent);}
}
}
