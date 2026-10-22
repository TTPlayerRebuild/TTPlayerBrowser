#pragma once
#include <windows.h>
#include <vector>

namespace ttpbrowser::testing {
struct ClientPixels {
    LONG width{}, height{};
    std::vector<DWORD> pixels;
};
// Render the real window paint path without desktop occlusion, cursor input,
// compositor scaling, or a browser child overwriting the background sample.
inline ClientPixels RenderClient(HWND window) {
    RECT client{};GetClientRect(window,&client);
    ClientPixels result{client.right,client.bottom,{}};
    if(result.width<=0 || result.height<=0)return result;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=result.width;info.bmiHeader.biHeight=-result.height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC dc=CreateCompatibleDC(nullptr);void* data{};
    HBITMAP bitmap=dc?CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0):nullptr;
    if(bitmap && data){
        const auto old=SelectObject(dc,bitmap);
        PatBlt(dc,0,0,result.width,result.height,BLACKNESS);
        SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
        GdiFlush();
        const auto* pixels=static_cast<const DWORD*>(data);
        result.pixels.assign(pixels,pixels+static_cast<size_t>(result.width)*result.height);
        SelectObject(dc,old);
    }
    if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);
    return result;
}
}
