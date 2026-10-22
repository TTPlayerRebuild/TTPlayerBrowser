#include "download_skin.h"
#include "ttplayer/skin/skin_package.h"
#include "ttplayer/core/text.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <msxml6.h>
#include <comdef.h>
#include <wrl/client.h>
#include <vector>
#include <string>

namespace ttpbrowser {
namespace {
using Microsoft::WRL::ComPtr;
#include "download_skin_data.inc"
struct Source {
    std::unique_ptr<ttplayer::skin::SkinPackage> package;
    HMODULE comm{GetModuleHandleW(L"ttpcomm.dll")};
    explicit Source(HMODULE module) {
        if(module)try {package=std::make_unique<ttplayer::skin::SkinPackage>(
            ttplayer::skin::SkinPackage::OpenResource(module,L"<DOWNLOAD_SKIN>"));}catch(...){}
    }
    std::vector<unsigned char> Read(const char* name) const {
        if(package)try{return package->ReadEntry(name,comm);}catch(...){}
        for(const auto& item:kDownloadAssets)if(!std::strcmp(item.name,name))return {item.bytes,item.bytes+item.size};
        return {};
    }
};
DownloadSkin::Bitmap DecodeBitmap(const std::vector<unsigned char>& bytes) {
    DownloadSkin::Bitmap result;
    if(bytes.size()<sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER))return result;
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};
    std::memcpy(&file,bytes.data(),sizeof(file));std::memcpy(&info,bytes.data()+sizeof(file),sizeof(info));
    if(file.bfType!=0x4d42 || info.biSize!=sizeof(info) || info.biWidth<=0 || info.biWidth>4096 ||
       !info.biHeight || info.biHeight<-4096 || info.biHeight>4096 || info.biPlanes!=1 ||
       info.biCompression!=BI_RGB || (info.biBitCount!=8 && info.biBitCount!=24 && info.biBitCount!=32))return result;
    const auto height=static_cast<size_t>(std::abs(info.biHeight));
    const auto stride=(static_cast<size_t>(info.biWidth)*info.biBitCount+31)/32*4;
    const auto palette=info.biBitCount==8?(info.biClrUsed?info.biClrUsed:256)*sizeof(RGBQUAD):0;
    if(file.bfOffBits<sizeof(file)+sizeof(info)+palette || file.bfOffBits>bytes.size() || stride*height>bytes.size()-file.bfOffBits)return result;
    HDC dc=GetDC(nullptr);void* pixels{};
    result.handle=CreateDIBSection(dc,reinterpret_cast<const BITMAPINFO*>(bytes.data()+sizeof(file)),DIB_RGB_COLORS,&pixels,nullptr,0);
    ReleaseDC(nullptr,dc);
    if(result.handle && pixels){std::memcpy(pixels,bytes.data()+file.bfOffBits,stride*height);result.size={info.biWidth,static_cast<LONG>(height)};}
    return result;
}
std::wstring Attribute(IXMLDOMDocument2* doc,const wchar_t* node,const wchar_t* attr) {
    if(!doc)return {};
    ComPtr<IXMLDOMNode> item,value;ComPtr<IXMLDOMNamedNodeMap> attrs;
    const auto query=std::wstring(L"/download_window/")+node;
    if(FAILED(doc->selectSingleNode(_bstr_t(query.c_str()),&item)) || !item || FAILED(item->get_attributes(&attrs)) || !attrs ||
       FAILED(attrs->getNamedItem(_bstr_t(attr),&value)) || !value)return {};
    VARIANT v{};if(FAILED(value->get_nodeValue(&v)))return {};
    _variant_t owned(v,false);return owned.vt==VT_BSTR && owned.bstrVal?owned.bstrVal:L"";
}
COLORREF Color(std::wstring text,COLORREF fallback) {
    unsigned r{},g{},b{};return swscanf_s(text.c_str(),L"#%2x%2x%2x",&r,&g,&b)==3?RGB(r,g,b):fallback;
}
RECT Rect(std::wstring text,RECT fallback) {
    RECT result{};return swscanf_s(text.c_str(),L"%ld,%ld,%ld,%ld",&result.left,&result.top,&result.right,&result.bottom)==4?result:fallback;
}
int Integer(std::wstring text,int fallback) {wchar_t* end{};const auto value=wcstol(text.c_str(),&end,10);return end!=text.c_str() && value>0 && value<=256?static_cast<int>(value):fallback;}
HFONT Font(IXMLDOMDocument2* doc,const wchar_t* node,const wchar_t* fallback,int pixels) {
    auto face=Attribute(doc,node,L"font");if(face.empty())face=fallback;
    return CreateFontW(Integer(Attribute(doc,node,L"font_size"),pixels),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,face.c_str());
}
}
DownloadSkin::DownloadSkin(HMODULE resources) {
    Source source(resources);host_resource=source.package!=nullptr;
    ComPtr<IXMLDOMDocument2> doc;
    if(FAILED(CoCreateInstance(__uuidof(DOMDocument60),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&doc)))) {
        CLSID cls{};if(SUCCEEDED(CLSIDFromProgID(L"Msxml2.DOMDocument.3.0",&cls)))CoCreateInstance(cls,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&doc));
    }
    if(doc) {
        doc->put_async(VARIANT_FALSE);doc->put_resolveExternals(VARIANT_FALSE);doc->put_validateOnParse(VARIANT_FALSE);
        doc->setProperty(_bstr_t(L"ProhibitDTD"),_variant_t(true));
        const auto bytes=source.Read("downwnd_skn.xml");const auto xml=ttplayer::core::Utf8ToWide(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
        VARIANT_BOOL loaded{};if(FAILED(doc->loadXML(_bstr_t(xml.c_str()),&loaded)) || loaded!=VARIANT_TRUE)doc.Reset();
    }
    auto attr=[&](const wchar_t* node,const wchar_t* name){return Attribute(doc.Get(),node,name);};
    link=Rect(attr(L"linklabel",L"position"),link);setting=Rect(attr(L"setting",L"position"),setting);
    banner=Rect(attr(L"banner",L"position"),banner);content=Rect(attr(L"splitter",L"position"),content);
    auto color=[&](COLORREF& value,const wchar_t* node,const wchar_t* name){value=Color(attr(node,name),value);};
    color(link_color,L"linklabel",L"color");color(link_background,L"linklabel",L"bkgnd");
    color(splitter_front,L"splitter",L"split_clr_front");color(splitter_background,L"splitter",L"split_clr_bkg");
    color(category_text,L"listsinfo",L"text_color");color(category_selection,L"listsinfo",L"selectclr");color(category_background,L"listsinfo",L"bk1clr");
    color(list_text,L"listinfo",L"text_color");color(list_selection,L"listinfo",L"selectclr");color(list_odd,L"listinfo",L"bk1clr");color(list_even,L"listinfo",L"bk2clr");
    color(header_text,L"header",L"text_color");color(progress,L"progress",L"progress_clr");color(progress_border,L"progress",L"progress_border");
    color(progress_text,L"progress",L"progress_text");color(disabled,L"progress",L"disable_clr");
    category_height=Integer(attr(L"listsinfo",L"item_height"),category_height);row_height=Integer(attr(L"listinfo",L"item_height"),row_height);
    link_font=Font(doc.Get(),L"linklabel",L"SimSun",12);category_font=Font(doc.Get(),L"listsinfo",L"Arial",12);
    list_font=Font(doc.Get(),L"listinfo",L"Arial",15);header_font=Font(doc.Get(),L"header",L"SimSun",12);
    const char* defaults[]={"banner.bmp","setting.bmp","header_field.bmp","line_s.bmp","sort_down.bmp","sort_up.bmp",
        "scrollbar.bmp","scrollbar_thumb.bmp","scrollbar_updown.bmp","scrollbar_arrow.bmp","finish.bmp","failed.bmp","downloading.bmp","play.bmp","pause.bmp","waiting.bmp"};
    const wchar_t* nodes[]={L"banner",L"setting",L"header",L"header",L"header",L"header",L"scrollbar",L"scrollbar",L"scrollbar",L"scrollbar",L"stateimage",L"stateimage",L"stateimage",L"stateimage",L"stateimage",L"stateimage"};
    const wchar_t* names[]={L"image",L"image",L"filedbkg",L"line_image",L"sort_down",L"sort_up",L"bar_image",L"thumb_image",L"buttons_image",L"arrow_image",L"state_finish",L"state_failed",L"state_downloading",L"state_play",L"state_pause",L"state_waiting"};
    for(int i=0;i<AssetCount;++i){auto name=ttplayer::core::WideToUtf8(attr(nodes[i],names[i]));if(name.empty())name=defaults[i];images_[i]=DecodeBitmap(source.Read(name.c_str()));}
}
DownloadSkin::~DownloadSkin() {
    for(auto& image:images_)if(image.handle)DeleteObject(image.handle);
    for(auto font:{link_font,category_font,list_font,header_font})if(font)DeleteObject(font);
}
void DownloadSkin::Draw(HDC dc,Asset asset,RECT destination,RECT source,bool transparent) const {
    const auto& image=images_[asset];if(!image.handle || destination.right<=destination.left || destination.bottom<=destination.top)return;
    if(source.right<=source.left || source.bottom<=source.top)source={0,0,image.size.cx,image.size.cy};
    const auto memory=CreateCompatibleDC(dc);const auto old=SelectObject(memory,image.handle);
    if(transparent)TransparentBlt(dc,destination.left,destination.top,destination.right-destination.left,destination.bottom-destination.top,
        memory,source.left,source.top,source.right-source.left,source.bottom-source.top,RGB(255,0,255));
    else StretchBlt(dc,destination.left,destination.top,destination.right-destination.left,destination.bottom-destination.top,
        memory,source.left,source.top,source.right-source.left,source.bottom-source.top,SRCCOPY);
    SelectObject(memory,old);DeleteDC(memory);
}
}
