#pragma once
#include <windows.h>
#include <array>

namespace ttpbrowser {
// Layout and bitmap bytes come from ttpres ZIP/<DOWNLOAD_SKIN>/downwnd_skn.xml.
class DownloadSkin {
public:
    enum Asset { Banner, Setting, Header, HeaderLine, SortDown, SortUp,
        ScrollBar, ScrollThumb, ScrollButtons, ScrollArrow,
        Finish, Failed, Downloading, Play, Pause, Waiting, AssetCount };
    struct Bitmap { HBITMAP handle{}; SIZE size{}; };
    explicit DownloadSkin(HMODULE resources);
    ~DownloadSkin();
    DownloadSkin(const DownloadSkin&)=delete;
    DownloadSkin& operator=(const DownloadSkin&)=delete;
    void Draw(HDC dc,Asset asset,RECT destination,RECT source={},bool transparent=false) const;
    const Bitmap& Image(Asset asset) const {return images_[asset];}
    RECT link{10,10,194,25},setting{368,7,453,32},banner{0,0,470,36},content{0,36,457,424};
    COLORREF link_color{RGB(81,125,174)},link_background{RGB(222,239,254)};
    COLORREF splitter_front{RGB(210,223,229)},splitter_background{RGB(234,244,255)};
    COLORREF category_text{RGB(55,80,146)},category_selection{RGB(176,215,255)},category_background{RGB(255,255,255)};
    COLORREF list_text{RGB(55,80,146)},list_selection{RGB(176,215,255)},list_odd{RGB(255,255,255)},list_even{RGB(241,248,255)};
    COLORREF header_text{RGB(35,50,69)},progress{RGB(107,171,215)},progress_border{RGB(199,216,224)},progress_text{RGB(66,88,137)},disabled{RGB(125,125,125)};
    int category_height{24},row_height{24};
    HFONT link_font{},category_font{},list_font{},header_font{};
    bool host_resource{};
private:
    std::array<Bitmap,AssetCount> images_{};
};
}
