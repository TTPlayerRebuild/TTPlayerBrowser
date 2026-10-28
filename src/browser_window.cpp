#include "browser_window.h"
#include "media_web_view.h"
#include <filesystem>
#include <fstream>
#include "downloads.h"
#include "download_page.h"
#include <commctrl.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <cstring>
#include <new>

namespace ttpbrowser {
namespace {
constexpr wchar_t kClass[] = L"TTPlayer_BrowserWnd";
constexpr UINT kActions = WM_APP + 0x321, kFallback = WM_APP + 0x322;
constexpr UINT_PTR kNavigationTimer = 101;
constexpr UINT_PTR kLinkTimer = 102;
constexpr int kRetry = 2001;
const wchar_t* const kLabels[]{L"关闭", L"音乐窗", L"下载管理", L"后退", L"前进", L"刷新", L"启动时显示", L"千千音乐"};
const std::wstring kDefaultLinkText=L"千千静听使用技巧";
const std::wstring kDefaultLinkUrl=L"http://ttplayer.qianqian.com/help.html?f=client";
LOGFONTW LinkFont(LOGFONTW description) {
    if(!description.lfHeight && !description.lfFaceName[0])
        GetObjectW(GetStockObject(DEFAULT_GUI_FONT),sizeof(description),&description);
    description.lfUnderline=TRUE;return description;
}
HBITMAP CopyBitmap(HBITMAP source) { return source ? static_cast<HBITMAP>(CopyImage(source, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION)) : nullptr; }
bool ValidBounds(const RECT& r) { return r.right - r.left >= 200 && r.bottom - r.top >= 150 && r.right - r.left <= 16384 && r.bottom - r.top <= 16384; }
std::wstring ParentDirectory(std::wstring path) { const auto slash = path.find_last_of(L"/\\"); return slash == path.npos ? L"" : path.substr(0, slash); }
void DrawBitmap(HDC dc, HBITMAP bitmap, RECT target, RECT source, COLORREF transparent, bool transparent_draw) {
    if (!bitmap || target.right <= target.left || target.bottom <= target.top) return;
    HDC memory = CreateCompatibleDC(dc); auto old = SelectObject(memory, bitmap);
    if (transparent_draw) TransparentBlt(dc, target.left, target.top, target.right - target.left, target.bottom - target.top,
        memory, source.left, source.top, source.right - source.left, source.bottom - source.top, transparent);
    else StretchBlt(dc, target.left, target.top, target.right - target.left, target.bottom - target.top,
        memory, source.left, source.top, source.right - source.left, source.bottom - source.top, SRCCOPY);
    SelectObject(memory, old); DeleteDC(memory);
}
void DrawBackground(HDC target, const ttp_browser_skin& skin, int width, int height) {
    const auto size = skin.background_size;
    if (!skin.background || size.cx <= 0 || size.cy <= 0 || width <= 0 || height <= 0) return;
    RECT resize = skin.resize_rect;
    resize.left = std::clamp<LONG>(resize.left, 0, size.cx);
    resize.top = std::clamp<LONG>(resize.top, 0, size.cy);
    resize.right = std::clamp<LONG>(resize.right, resize.left, size.cx);
    resize.bottom = std::clamp<LONG>(resize.bottom, resize.top, size.cy);
    if (IsRectEmpty(&resize)) {
        // Legacy browser_window descriptors (including Default_Skin) omit
        // resize_rect. Expand a central pixel band instead of scaling their
        // frame, rounded corners and toolbar/footer artwork with the window.
        resize = {size.cx/2, size.cy/2, size.cx/2+1, size.cy/2+1};
    }
    const int source_x[]{0,resize.left,resize.right,size.cx}, source_y[]{0,resize.top,resize.bottom,size.cy};
    const int left=std::min<int>(resize.left,width),top=std::min<int>(resize.top,height);
    const int destination_x[]{0,left,std::max<int>(left,width-(size.cx-resize.right)),width};
    const int destination_y[]{0,top,std::max<int>(top,height-(size.cy-resize.bottom)),height};
    HDC source = CreateCompatibleDC(target); const auto previous = SelectObject(source,skin.background);
    const int stretch_mode=SetStretchBltMode(target,COLORONCOLOR);
    for (int row=0;row<3;++row) for (int column=0;column<3;++column) {
        RECT from{source_x[column],source_y[row],source_x[column+1],source_y[row+1]};
        const RECT to{destination_x[column],destination_y[row],destination_x[column+1],destination_y[row+1]};
        // A programmatic size smaller than the fixed margins crops artwork at
        // its inner edge; it must not reintroduce whole-frame scaling.
        if(column==0)from.right=from.left+(to.right-to.left);
        if(column==2)from.left=from.right-(to.right-to.left);
        if(row==0)from.bottom=from.top+(to.bottom-to.top);
        if(row==2)from.top=from.bottom-(to.bottom-to.top);
        const int sw=from.right-from.left,sh=from.bottom-from.top,dw=to.right-to.left,dh=to.bottom-to.top;
        if (sw<=0 || sh<=0 || dw<=0 || dh<=0) continue;
        if (skin.resize_tile && (row==1 || column==1)) {
            const int saved=SaveDC(target); IntersectClipRect(target,to.left,to.top,to.right,to.bottom);
            for (int y=to.top;y<to.bottom;y+=sh) for (int x=to.left;x<to.right;x+=sw)
                BitBlt(target,x,y,sw,sh,source,from.left,from.top,SRCCOPY);
            RestoreDC(target,saved);
        } else if(sw==dw && sh==dh) BitBlt(target,to.left,to.top,dw,dh,source,from.left,from.top,SRCCOPY);
        else StretchBlt(target,to.left,to.top,dw,dh,source,from.left,from.top,sw,sh,SRCCOPY);
    }
    if(stretch_mode)SetStretchBltMode(target,stretch_mode);
    SelectObject(source,previous); DeleteDC(source);
}
HRGN SkinRegion(const ttp_browser_skin& skin, SIZE size, RECT content) {
    if (size.cx <= 0 || size.cy <= 0) return nullptr;
    if (!skin.valid || !skin.background) return CreateRectRgn(0,0,size.cx,size.cy);
    // Build the mask from precisely the same nine-patch renderer as WM_PAINT.
    // A region also clips compositor/browser child surfaces at the skin edges;
    // LWA_COLORKEY alone does not define the HWND's geometry.
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=size.cx; info.bmiHeader.biHeight=-size.cy;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    HDC dc=CreateCompatibleDC(nullptr); void* raw{};
    HBITMAP bitmap=dc ? CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0) : nullptr;
    if (!bitmap || !raw) { if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return nullptr; }
    auto previous=SelectObject(dc,bitmap);
    const RECT bounds{0,0,size.cx,size.cy}; HBRUSH brush=CreateSolidBrush(skin.transparent_color);
    FillRect(dc,&bounds,brush); DeleteObject(brush); DrawBackground(dc,skin,size.cx,size.cy);
    // GDI drawing must complete before examining the DIB's shared pixel memory.
    GdiFlush();
    const DWORD key=(GetRValue(skin.transparent_color)<<16)|(GetGValue(skin.transparent_color)<<8)|GetBValue(skin.transparent_color);
    const auto* pixels=static_cast<const DWORD*>(raw);
    std::vector<RECT> spans;
    bool excessive=false;
    try {
        for (LONG y=0;y<size.cy && !excessive;++y) {
            LONG start=-1;
            for (LONG x=0;x<=size.cx;++x) {
                const bool opaque=x<size.cx && (pixels[static_cast<size_t>(y)*size.cx+x]&0x00ffffff)!=key;
                if (opaque && start<0) start=x;
                else if (!opaque && start>=0) {
                    spans.push_back({start,y,x,y+1}); start=-1;
                    // Avoid pathological resource artwork creating an enormous
                    // GDI region; the color key still draws that fallback safely.
                    if (spans.size()>=65536) { excessive=true;break; }
                }
            }
        }
    } catch (...) { excessive=true; }
    SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc);
    HRGN region{};
    if (excessive) region=CreateRectRgn(0,0,size.cx,size.cy);
    else if (spans.empty()) region=CreateRectRgn(0,0,0,0);
    else {
        const size_t bytes=sizeof(RGNDATAHEADER)+spans.size()*sizeof(RECT);
        std::vector<unsigned char> data(bytes);
        auto* description=reinterpret_cast<RGNDATA*>(data.data());
        description->rdh.dwSize=sizeof(RGNDATAHEADER); description->rdh.iType=RDH_RECTANGLES;
        description->rdh.nCount=static_cast<DWORD>(spans.size()); description->rdh.nRgnSize=static_cast<DWORD>(spans.size()*sizeof(RECT));
        description->rdh.rcBound=bounds;
        memcpy(description->Buffer,spans.data(),spans.size()*sizeof(RECT));
        region=ExtCreateRegion(nullptr,static_cast<DWORD>(bytes),description);
    }
    // The declared browser/download rectangle is an opaque child surface even
    // if a frame-only skin bitmap leaves its center in the transparent key.
    RECT clipped{};
    if (region && IntersectRect(&clipped,&content,&bounds)) {
        HRGN center=CreateRectRgnIndirect(&clipped);
        if (center) { CombineRgn(region,region,center,RGN_OR);DeleteObject(center); }
    }
    return region;
}
}
BrowserWindow::BrowserWindow(const ttp_browser_host& host, const ttp_browser_config& config, Downloads& downloads, media::Library& library)
    : host_(host), downloads_(downloads), bridge_(std::make_shared<BridgeContext>()) {
    HMODULE module{}; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&BrowserWindow::Proc), &module); instance_ = module;
    media_view_=std::make_unique<MediaSurface>(library,host,downloads,config.state_file,config.engine);
    RefreshDownloadPage();
    const auto preference=std::filesystem::path(config.state_file).parent_path()/L"PersonalLibrary.enabled";
    personal_view_=!config.home_url || std::wstring(config.home_url)==L"http://mini.qianqian.com/index.html";
    {std::ifstream saved(preference);char choice{};if(saved.get(choice)){
        download_page_view_=choice=='2'&&!download_page_url_.empty();
        personal_view_=!download_page_view_&&choice!='0';
    }}
    personal_view_=personal_view_ && host_.media_source_version!=0;
    state_ = config.initial_state; state_.size = sizeof(state_);
    first_position_ = !ValidBounds(config.initial_state.bounds);
    if (!ValidBounds(state_.bounds)) state_.bounds = {140, 100, 700, 640};
    autostart_ = (state_.flags & TTP_BROWSER_STATE_AUTOSTART) != 0;
    downloads_view_ = (state_.flags & TTP_BROWSER_STATE_DOWNLOADS) != 0;
    bridge_->allowed = [this] {
        return !destroying_ && !hide_requested_ && window_ && IsWindowVisible(window_) && !downloads_view_ && !personal_view_ && !download_page_view_ && browser_ready_ && backend_ &&
            !error_visible_ && !origin_.empty() && HttpOrigin(backend_->Source()) == origin_;
    };
    bridge_->submit = [this](std::vector<BrowserTrack> tracks, int a, int b, int c, bool downloads_request, uint64_t generation) {
        if (!downloads_request && !host_.submit_tracks) return E_NOTIMPL;
        Action action; action.kind = downloads_request ? Action::Download : Action::Submit;
        action.tracks = std::move(tracks); action.modes[0] = a; action.modes[1] = b; action.modes[2] = c; action.document_generation = generation;
        return Queue(std::move(action));
    };
    bridge_->query = [this](uint32_t kind, uint32_t selector, BSTR* value) {
        return host_.query_info ? host_.query_info(host_.context, kind, selector, value) : E_NOTIMPL;
    };
    bridge_->setup = [this](std::wstring a, std::wstring b, std::wstring c, uint64_t generation) {
        if (!host_.setup_pack) return E_NOTIMPL;
        Action action; action.kind = Action::Setup; action.a = std::move(a); action.b = std::move(b); action.c = std::move(c); action.document_generation = generation;
        return Queue(std::move(action));
    };
    bridge_->hyperlink = [this](std::wstring text, std::wstring url, uint64_t generation) {
        Action action; action.kind = Action::Hyperlink; action.a = std::move(text); action.b = std::move(url); action.document_generation = generation; return Queue(std::move(action));
    };
    Configure(config);
}
BrowserWindow::~BrowserWindow() {
    destroying_ = true;
    bridge_->alive = false; bridge_->allowed = {}; bridge_->submit = {}; bridge_->query = {}; bridge_->setup = {};
    bridge_->hyperlink = {};
    actions_.clear();
    if (backend_) backend_->Shutdown(); backend_.reset(); external_.Reset();
    if (window_) { KillTimer(window_, kNavigationTimer);KillTimer(window_,kLinkTimer); DestroyWindow(window_); }
    media_view_.reset();
    FreeSkin();
    // Another instance may still own a window of this class; Windows refuses
    // that unregister until its final HWND is gone.
    UnregisterClassW(kClass, instance_);
}
HRESULT BrowserWindow::Configure(const ttp_browser_config& config) {
    const std::wstring home = config.home_url && *config.home_url ? config.home_url : L"http://mini.qianqian.com/index.html";
    const auto origin = HttpOrigin(home); if (origin.empty()) return E_INVALIDARG;
    const bool changed = !home_url_.empty() && home_url_ != home;
    if (config.engine > 2) return E_INVALIDARG;
    const bool changed_engine = engine_ != config.engine;
    engine_ = config.engine;
    media_view_->Configure(engine_);
    bridge_->accept_recommendations = config.accept_recommendations != FALSE;
    state_file_ = config.state_file ? config.state_file : L"";
    home_url_ = home; origin_ = origin; topmost_ = config.topmost != FALSE; opacity_ = config.opacity;
    if (!opacity_) opacity_ = 255;
    profile_ = ParentDirectory(state_file_);
    if (profile_.empty()) {
        wchar_t local[MAX_PATH]{};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, local))) profile_ = local;
        else { wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp); profile_ = temp; }
        profile_ += L"\\TTPlayer";
    }
    profile_ += L"\\MusicWindow.WebView2";
    RefreshDownloadPage();
    if (window_) {
        SetWindowPos(window_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetLayeredWindowAttributes(window_, skin_.valid ? skin_.transparent_color : RGB(255,0,255), opacity_, LWA_COLORKEY | LWA_ALPHA);
        if (changed_engine && backend_) {
            backend_->Shutdown(); backend_.reset(); browser_ready_ = false;
            if (IsWindowVisible(window_) && !downloads_view_ && !personal_view_) StartBrowser();
        } else if (changed && backend_ && !download_page_view_) backend_->Navigate(home_url_);
    }
    return S_OK;
}
void BrowserWindow::FreeSkin() {
    if (skin_.background) DeleteObject(skin_.background);
    for (auto& control : skin_.controls) if (control.image) DeleteObject(control.image);
    skin_ = {};
}
HRESULT BrowserWindow::SetSkin(const ttp_browser_skin& skin) {
    ttp_browser_skin copy = skin;
    copy.background = CopyBitmap(skin.background);
    for (size_t i = 0; i < TTP_BROWSER_CONTROL_COUNT; ++i) copy.controls[i].image = CopyBitmap(skin.controls[i].image);
    if ((skin.background && !copy.background) || std::any_of(std::begin(copy.controls), std::end(copy.controls),
        [&skin, &copy](const auto& item) { const auto index = &item - copy.controls; return skin.controls[index].image && !item.image; })) {
        if (copy.background) DeleteObject(copy.background); for (auto& c : copy.controls) if (c.image) DeleteObject(c.image); return E_OUTOFMEMORY;
    }
    FreeSkin(); skin_ = copy;
    region_dirty_ = true;
    if (first_position_ && skin_.valid && ValidBounds(skin_.position)) {
        state_.bounds = skin_.position; first_position_ = false;
        if (window_) SetWindowPos(window_, nullptr, state_.bounds.left, state_.bounds.top,
            state_.bounds.right - state_.bounds.left, state_.bounds.bottom - state_.bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (window_) {
        SetLayeredWindowAttributes(window_, skin_.valid ? skin_.transparent_color : RGB(255,0,255), opacity_, LWA_COLORKEY | LWA_ALPHA);
        UpdateWindowRegion(); Layout();
        RedrawWindow(window_,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_NOERASE);
    }
    return S_OK;
}
HRESULT BrowserWindow::EnsureWindow() {
    if (destroying_ || (host_.owner && !IsWindow(host_.owner))) return RPC_E_DISCONNECTED;
    if (window_) return S_OK;
    WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc = Proc; cls.hInstance = instance_;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.lpszClassName = kClass;
    cls.hIcon = LoadIconW(host_.resources ? host_.resources : GetModuleHandleW(nullptr), MAKEINTRESOURCEW(128));
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return HRESULT_FROM_WIN32(GetLastError());
    auto bounds = state_.bounds;
    HMONITOR monitor = MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST); MONITORINFO info{sizeof(info)};
    if (GetMonitorInfoW(monitor, &info)) {
        const LONG width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        bounds.left = std::clamp(bounds.left, info.rcWork.left, std::max(info.rcWork.left, info.rcWork.right - width));
        bounds.top = std::clamp(bounds.top, info.rcWork.top, std::max(info.rcWork.top, info.rcWork.bottom - height));
        bounds.right = bounds.left + width; bounds.bottom = bounds.top + height;
    }
    // 00467B9B creates Browser as WS_POPUP/WS_EX_TOOLWINDOW. A thick frame
    // reintroduces themed/DWM borders even when WM_NCCALCSIZE returns zero.
    HWND created = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, kClass, L"千千音乐窗", WS_POPUP | WS_CLIPCHILDREN,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        host_.owner, nullptr, instance_, this);
    if (!created) return HRESULT_FROM_WIN32(GetLastError());
    SetLayeredWindowAttributes(window_, skin_.valid ? skin_.transparent_color : RGB(255,0,255), opacity_, LWA_COLORKEY | LWA_ALPHA);
    SetWindowPos(window_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return S_OK;
}
void BrowserWindow::ApplyFramePolicy() {
    if (!window_) return;
    // Keep XP imports unchanged: DWM is optional and its newer attributes are
    // ignored by older versions. Load only the system DLL, never a cwd sibling.
    wchar_t system[MAX_PATH]{}; const UINT count=GetSystemDirectoryW(system,MAX_PATH);
    if (!count || count>=MAX_PATH) return;
    std::wstring path(system,count); path+=L"\\dwmapi.dll";
    HMODULE dwm=LoadLibraryW(path.c_str()); if (!dwm) return;
    using SetAttribute=HRESULT(WINAPI*)(HWND,DWORD,LPCVOID,DWORD);
    auto set=reinterpret_cast<SetAttribute>(GetProcAddress(dwm,"DwmSetWindowAttribute"));
    if (set) {
        const DWORD nc_disabled=1, never_round=1; const COLORREF no_border=0xfffffffe;
        set(window_,2,&nc_disabled,sizeof(nc_disabled));   // DWMWA_NCRENDERING_POLICY / DWMNCRP_DISABLED
        set(window_,33,&never_round,sizeof(never_round)); // DWMWA_WINDOW_CORNER_PREFERENCE / DONOTROUND
        set(window_,34,&no_border,sizeof(no_border));     // DWMWA_BORDER_COLOR / DWMWA_COLOR_NONE
    }
    FreeLibrary(dwm);
}
void BrowserWindow::UpdateWindowRegion() {
    if (!window_ || updating_region_) return;
    RECT client{}; GetClientRect(window_,&client); const SIZE size{client.right,client.bottom};
    if (size.cx<=0 || size.cy<=0 || (!region_dirty_ && size.cx==region_size_.cx && size.cy==region_size_.cy)) return;
    HRGN region=SkinRegion(skin_,size,ContentRect());
    if (!region) return;
    // SetWindowRgn can synchronously send WINDOWPOS/SIZE notifications.
    // Publish the cache and guard before entering that call.
    updating_region_=true;
    if (SetWindowRgn(window_,region,TRUE)) { region_size_=size;region_dirty_=false; }
    else DeleteObject(region); // Ownership transfers to Windows only on success.
    updating_region_=false;
}
SIZE BrowserWindow::MinimumSize() const {
    return {skin_.valid ? std::max(320L,skin_.background_size.cx) : 320,
            skin_.valid ? std::max(personal_view_?440L:240L,skin_.background_size.cy) : personal_view_?440:240};
}
int BrowserWindow::BackgroundHit(POINT point) const {
    RECT client{}; GetClientRect(window_,&client);
    if (!PtInRect(&client,point) || Hit(point)>=0) return HTCLIENT;
    const bool left=point.x<5,right=point.x>=client.right-5,top=point.y<4,bottom=point.y>=client.bottom-5;
    // Extend corner grips along the adjacent opaque edges. A rounded skin's
    // extreme corner is outside its region and cannot receive mouse input.
    constexpr LONG corner=16;
    if ((top && point.x<corner) || (left && point.y<corner)) return HTTOPLEFT;
    if ((top && point.x>=client.right-corner) || (right && point.y<corner)) return HTTOPRIGHT;
    if ((bottom && point.x<corner) || (left && point.y>=client.bottom-corner)) return HTBOTTOMLEFT;
    if ((bottom && point.x>=client.right-corner) || (right && point.y>=client.bottom-corner)) return HTBOTTOMRIGHT;
    if (left) return HTLEFT; if (right) return HTRIGHT; if (top) return HTTOP; if (bottom) return HTBOTTOM;
    const RECT content=ContentRect();
    return PtInRect(&content,point) ? HTCLIENT : HTCAPTION;
}
void BrowserWindow::BeginBackgroundDrag(POINT point) {
    const int hit=BackgroundHit(point); if (hit==HTCLIENT) return;
    drag_hit_=hit; drag_anchor_=point; ClientToScreen(window_,&drag_anchor_);
    GetWindowRect(window_,&drag_bounds_); pressed_=-1; hover_=-1;
    SetCapture(window_); SendMessageW(window_,WM_ENTERSIZEMOVE,0,0);
}
void BrowserWindow::ContinueBackgroundDrag() {
    if (drag_hit_==HTNOWHERE || GetCapture()!=window_) return;
    POINT point{}; GetCursorPos(&point); const LONG dx=point.x-drag_anchor_.x,dy=point.y-drag_anchor_.y;
    RECT bounds=drag_bounds_;
    const auto snap = [&] {
        if (!host_.adjust_window_rect) return;
        RECT adjusted=bounds;
        if (host_.adjust_window_rect(host_.context,window_,drag_hit_,&adjusted)==S_OK)
            bounds=adjusted;
    };
    if (drag_hit_==HTCAPTION) { OffsetRect(&bounds,dx,dy);snap(); }
    else {
        const bool left=drag_hit_==HTLEFT || drag_hit_==HTTOPLEFT || drag_hit_==HTBOTTOMLEFT;
        const bool right=drag_hit_==HTRIGHT || drag_hit_==HTTOPRIGHT || drag_hit_==HTBOTTOMRIGHT;
        const bool top=drag_hit_==HTTOP || drag_hit_==HTTOPLEFT || drag_hit_==HTTOPRIGHT;
        const bool bottom=drag_hit_==HTBOTTOM || drag_hit_==HTBOTTOMLEFT || drag_hit_==HTBOTTOMRIGHT;
        if(left)bounds.left+=dx; if(right)bounds.right+=dx; if(top)bounds.top+=dy; if(bottom)bounds.bottom+=dy;
        // Keep the fixed edges anchored, then apply skin/work-area limits after
        // magnetic correction, as the original common resize path does.
        snap();
        const SIZE minimum=MinimumSize(); MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromRect(&bounds,MONITOR_DEFAULTTONEAREST),&monitor);
        const LONG maximum_width=std::max(minimum.cx,monitor.rcWork.right-monitor.rcWork.left);
        const LONG maximum_height=std::max(minimum.cy,monitor.rcWork.bottom-monitor.rcWork.top);
        if(left)bounds.left=bounds.right-std::clamp(bounds.right-bounds.left,minimum.cx,maximum_width);
        else if(right)bounds.right=bounds.left+std::clamp(bounds.right-bounds.left,minimum.cx,maximum_width);
        if(top)bounds.top=bounds.bottom-std::clamp(bounds.bottom-bounds.top,minimum.cy,maximum_height);
        else if(bottom)bounds.bottom=bounds.top+std::clamp(bounds.bottom-bounds.top,minimum.cy,maximum_height);
    }
    SetWindowPos(window_,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOZORDER|SWP_NOACTIVATE);
    RedrawWindow(window_,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW|RDW_NOERASE);
}
void BrowserWindow::EndBackgroundDrag(bool cancel) {
    if (drag_hit_==HTNOWHERE) return;
    drag_hit_=HTNOWHERE;
    if (cancel && window_) SetWindowPos(window_,nullptr,drag_bounds_.left,drag_bounds_.top,
        drag_bounds_.right-drag_bounds_.left,drag_bounds_.bottom-drag_bounds_.top,SWP_NOZORDER|SWP_NOACTIVATE);
    if (GetCapture()==window_) ReleaseCapture();
    if (window_) { SendMessageW(window_,WM_EXITSIZEMOVE,0,0);InvalidateRect(window_,nullptr,FALSE); }
}
RECT BrowserWindow::ContentRect() const {
    RECT client{}; if (window_) GetClientRect(window_, &client);
    RECT result{10, 36, std::max(11L, client.right - 10), std::max(37L, client.bottom - 30)};
    if (skin_.valid && skin_.background_size.cx > 0 && skin_.background_size.cy > 0) {
        result = skin_.content; result.right += client.right - skin_.background_size.cx; result.bottom += client.bottom - skin_.background_size.cy;
        result.right = std::max(result.left + 1, result.right); result.bottom = std::max(result.top + 1, result.bottom);
    }
    return result;
}
RECT BrowserWindow::ControlRect(int id) const {
    RECT client{}; GetClientRect(window_, &client);
    if (skin_.valid) {
        RECT rect = skin_.controls[id].bounds;
        const int dx = client.right - skin_.background_size.cx, dy = client.bottom - skin_.background_size.cy;
        if (id == TTP_BROWSER_CLOSE || id == TTP_BROWSER_LINK) OffsetRect(&rect, dx, 0);
        if (id == TTP_BROWSER_STARTUP || id == TTP_BROWSER_LINK) OffsetRect(&rect, 0, dy);
        return rect;
    }
    switch (id) {
    case TTP_BROWSER_CLOSE: return {client.right - 34, 5, client.right - 8, 29};
    case TTP_BROWSER_MUSIC: return {115, 5, 185, 29};
    case TTP_BROWSER_DOWNLOADS: return {188, 5, 268, 29};
    case TTP_BROWSER_BACK: return {10, 5, 40, 29};
    case TTP_BROWSER_FORWARD: return {42, 5, 72, 29};
    case TTP_BROWSER_REFRESH: return {74, 5, 108, 29};
    case TTP_BROWSER_STARTUP: return {10, client.bottom - 25, 150, client.bottom - 5};
    default: return {std::max(152L, client.right - 230), client.bottom - 25, client.right - 10, client.bottom - 5};
    }
}
bool BrowserWindow::Enabled(int id) const {
    if (id == TTP_BROWSER_BACK) return !downloads_view_ && (personal_view_ ? media_view_->CanBack() : can_back_);
    if (id == TTP_BROWSER_FORWARD) return !downloads_view_ && !personal_view_ && can_forward_;
    if (id == TTP_BROWSER_REFRESH) return !downloads_view_;
    return true;
}
int BrowserWindow::Hit(POINT point) const {
    for (int i = 0; i < TTP_BROWSER_CONTROL_COUNT; ++i) {
        const auto bounds = i==TTP_BROWSER_LINK ? LinkHitRect() : ControlRect(i);
        if (PtInRect(&bounds, point)) return i;
    }
    return -1;
}
void BrowserWindow::UpdateTooltips() {
    if (!tooltip_) return;
    for (int i = 0; i < TTP_BROWSER_CONTROL_COUNT; ++i) {
        const auto bounds=i==TTP_BROWSER_LINK ? LinkHitRect() : ControlRect(i);
        TOOLINFOW tool{sizeof(tool)}; tool.hwnd = window_; tool.uId = i + 1; tool.rect = bounds;
        // GETTOOLINFO writes text into lpszText when non-null. Query only the
        // registration here; never pass the read-only label as an output buffer.
        // ADDTOOL below borrows that label as input, which is a different contract.
        if (!SendMessageW(tooltip_, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&tool))) {
            tool.hwnd = window_; tool.uId = i + 1; tool.rect = bounds; tool.uFlags = TTF_SUBCLASS;
            tool.lpszText = i==TTP_BROWSER_LINK ? LPSTR_TEXTCALLBACKW : const_cast<wchar_t*>(kLabels[i]);
            SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        } else { tool.rect = bounds; SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool)); }
    }
}
void BrowserWindow::Layout() {
    if (!window_) return;
    const auto bounds = ContentRect(); if (backend_) backend_->Resize(bounds);
    if (downloads_.Window()) downloads_.Resize(bounds);
    if (media_view_) media_view_->Resize(bounds);
    if (error_panel_) {
        MoveWindow(error_panel_, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, TRUE);
        const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        // Error controls are children of the main HWND so the retry command is
        // delivered to our WndProc, while their positions overlay the panel.
        MoveWindow(error_text_, bounds.left + 22, bounds.top + std::max(16, height / 2 - 70), std::max(1, width - 44), 120, TRUE);
        MoveWindow(retry_button_, bounds.left + std::max(12, width / 2 - 55), bounds.top + std::min(height - 38, height / 2 + 62), 110, 28, TRUE);
    }
    RefreshLinkHover();ResetLinkAnimation();UpdateTooltips();
}
HRESULT BrowserWindow::StartBrowser(bool force_ie) {
    if (backend_) { backend_->Shutdown(); backend_.reset(); }
    browser_ready_ = false; modern_attempt_ = false;
    if (!external_) { HRESULT hr = CreateBrowserDispatch(bridge_, true, &external_); if (FAILED(hr)) return hr; }
    force_ie = force_ie || engine_ == 1;
    if (!force_ie) { backend_ = MakeWebViewBackend(); modern_attempt_ = backend_ != nullptr; }
    if (!backend_ && engine_ == 2) { Error(L"未检测到可用的 WebView2 Runtime。"); return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND); }
    if (!backend_) backend_ = MakeIeBackend();
    BrowserEvents events;
    events.ready = [this](const wchar_t* name) { browser_ready_ = true; backend_name_ = name; };
    events.starting = [this](const std::wstring& target) { NavigationStarting(target); };
    events.completed = [this](bool success, const std::wstring& error) { NavigationCompleted(success, error); };
    events.history = [this](bool back, bool forward) { can_back_ = back; can_forward_ = forward; if (window_) InvalidateRect(window_, nullptr, FALSE); };
    events.trusted = [this](const std::wstring& target) { return !download_page_view_ && !origin_.empty() && HttpOrigin(target) == origin_; };
    if(download_page_view_)events.intercept_download=[this](const std::wstring& page,const std::wstring& target){return InterceptPageDownload(page,target);};
    if(download_page_view_&&host_.get_page_script&&host_.recognize_page_audio) {
        BSTR script{};if(host_.get_page_script(host_.context,&script)==S_OK&&script)events.adapter_script.assign(script,SysStringLen(script));SysFreeString(script);
        if(!events.adapter_script.empty())events.adapter_message=[this](const std::wstring& page,const std::wstring& json){PageMessage(page,json);};
    }
    events.download = [this](const std::wstring& source) {
        Action action; action.kind = Action::Download; BrowserTrack track; track.source = source;
        action.tracks.push_back(std::move(track)); Queue(std::move(action));
    };
    const HRESULT hr = backend_->Start(window_, ContentRect(), download_page_view_?nullptr:external_.Get(), profile_, std::move(events));
    if (FAILED(hr)) {
        const bool retry_ie = modern_attempt_ && !force_ie && engine_ == 0; backend_->Shutdown(); backend_.reset();
        if (retry_ie) return StartBrowser(true);
        Error(L"无法创建网页浏览器。请检查系统浏览器组件或 WebView2 Runtime。"); return hr;
    }
    backend_->Visible(!downloads_view_ && !personal_view_); loading_ = true;
    SetTimer(window_, kNavigationTimer, 30000, nullptr);
    return backend_->Navigate(current_url_.empty() ? (download_page_view_?download_page_url_:home_url_) : current_url_);
}
void BrowserWindow::NavigationStarting(const std::wstring& target) {
    ++bridge_->document_generation;
    current_url_ = target; loading_ = true; error_visible_ = false; status_ = L"正在打开音乐窗…";
    ShowWindow(error_panel_, SW_HIDE); ShowWindow(error_text_, SW_HIDE); ShowWindow(retry_button_, SW_HIDE);
    if (backend_) backend_->Visible(!downloads_view_ && !personal_view_);
    SetTimer(window_, kNavigationTimer, 30000, nullptr); InvalidateRect(window_, nullptr, FALSE);
}
void BrowserWindow::NavigationCompleted(bool success, const std::wstring& error) {
    KillTimer(window_, kNavigationTimer); loading_ = false;
    if (!success) {
        if (modern_attempt_ && !browser_ready_ && engine_ == 0) { PostMessageW(window_, kFallback, 0, 0); return; }
        Error(error.empty() ? L"音乐网页加载失败。" : error); return;
    }
    error_visible_ = false; status_.clear();
    ShowWindow(error_panel_, SW_HIDE); ShowWindow(error_text_, SW_HIDE); ShowWindow(retry_button_, SW_HIDE);
    if (backend_) backend_->Visible(!downloads_view_ && !personal_view_); InvalidateRect(window_, nullptr, FALSE);
}
void BrowserWindow::Error(const std::wstring& message) {
    if (!window_) return;
    KillTimer(window_, kNavigationTimer); loading_ = false; error_visible_ = true; status_ = message;
    const std::wstring text = L"音乐窗暂时无法打开\r\n\r\n" + message + L"\r\n" + (current_url_.empty() ? home_url_ : current_url_);
    SetWindowTextW(error_text_, text.c_str());
    if (backend_) backend_->Visible(false);
    ShowWindow(error_panel_, (downloads_view_ || personal_view_) ? SW_HIDE : SW_SHOW);
    ShowWindow(error_text_, (downloads_view_ || personal_view_) ? SW_HIDE : SW_SHOW);
    ShowWindow(retry_button_, (downloads_view_ || personal_view_) ? SW_HIDE : SW_SHOW);
    if (!downloads_view_ && !personal_view_) { BringWindowToTop(error_panel_); BringWindowToTop(error_text_); BringWindowToTop(retry_button_); }
    InvalidateRect(window_, nullptr, FALSE);
}
void BrowserWindow::Retry() {
    if (!backend_ || !browser_ready_) { StartBrowser(); return; }
    const HRESULT hr = backend_->Navigate(current_url_.empty() ? (download_page_view_?download_page_url_:home_url_) : current_url_);
    if (FAILED(hr)) Error(L"无法重新打开音乐网页。");
}
void BrowserWindow::SwitchView(bool downloads_view) {
    downloads_view_ = downloads_view;
    if(media_view_->Window())ShowWindow(media_view_->Window(),SW_HIDE);
    if (downloads_view_) {
        if (!downloads_.Window()) downloads_.CreateView(window_);
        if (downloads_.Window()) { downloads_.Resize(ContentRect()); downloads_.Refresh(); ShowWindow(downloads_.Window(), SW_SHOW); }
        if (backend_) backend_->Visible(false);
        ShowWindow(error_panel_, SW_HIDE); ShowWindow(error_text_, SW_HIDE); ShowWindow(retry_button_, SW_HIDE);
    } else {
        if (downloads_.Window()) ShowWindow(downloads_.Window(), SW_HIDE);
        if(personal_view_) {
            RECT outer{};GetWindowRect(window_,&outer);const auto minimum=MinimumSize();
            if(outer.right-outer.left<minimum.cx||outer.bottom-outer.top<minimum.cy)
                SetWindowPos(window_,nullptr,0,0,std::max(outer.right-outer.left,minimum.cx),std::max(outer.bottom-outer.top,minimum.cy),SWP_NOMOVE|SWP_NOZORDER);
            if(backend_)backend_->Visible(false);
            ShowWindow(error_panel_,SW_HIDE);ShowWindow(error_text_,SW_HIDE);ShowWindow(retry_button_,SW_HIDE);
            media_view_->Create(window_);media_view_->Resize(ContentRect());ShowWindow(media_view_->Window(),SW_SHOW);
        }
        else if (!backend_) StartBrowser();
        else if (error_visible_) Error(status_);
        else backend_->Visible(true);
    }
    InvalidateRect(window_, nullptr, FALSE); StateChanged();
}
void BrowserWindow::PersonalView(bool personal) {
    if(download_page_view_){if(backend_)backend_->Shutdown();backend_.reset();browser_ready_=false;current_url_.clear();}
    download_page_view_=false;
    ++bridge_->document_generation;personal_view_=personal;
    const auto file=std::filesystem::path(state_file_).parent_path()/L"PersonalLibrary.enabled";
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(),error);std::ofstream marker(file);marker<<(personal?'1':'0');
    SwitchView(false);
}
void BrowserWindow::DownloadPageView() {
    RefreshDownloadPage();
    if(download_page_url_.empty())return;
    if(!download_page_view_){
        ++bridge_->document_generation;
        if(backend_)backend_->Shutdown();backend_.reset();browser_ready_=false;
        current_url_=download_page_url_;can_back_=can_forward_=false;
    }
    download_page_view_=true;personal_view_=false;
    const auto file=std::filesystem::path(state_file_).parent_path()/L"PersonalLibrary.enabled";
    std::error_code error;std::filesystem::create_directories(file.parent_path(),error);std::ofstream(file)<<'2';
    SwitchView(false);
}
void BrowserWindow::RefreshDownloadPage() {
    const auto page=GetDownloadPage(host_);
    const bool available=!page.name.empty()&&!HttpOrigin(page.url).empty();
    download_page_name_=available?page.name:L"";
    download_page_url_=available?page.url:L"";
    if(!available&&download_page_view_) {
        if(window_)PersonalView(host_.media_source_version!=0);
        else {download_page_view_=false;personal_view_=host_.media_source_version!=0;current_url_.clear();}
    }
}
bool BrowserWindow::InterceptPageDownload(const std::wstring& page,const std::wstring& target) {
    if(!download_page_view_||destroying_||!host_.recognize_page_download)return false;
    BSTR filename{};const auto hr=host_.recognize_page_download(host_.context,page.c_str(),target.c_str(),&filename);
    if(hr!=S_OK){SysFreeString(filename);return false;}
    Action action;action.kind=Action::PackageDownload;action.a=target;action.page_action=true;
    if(filename)action.b.assign(filename,SysStringLen(filename));SysFreeString(filename);
    const auto queued=Queue(std::move(action));if(FAILED(queued))PageResult(queued,L"");return true;
}
void BrowserWindow::PageMessage(const std::wstring& page,const std::wstring& json) {
    if(!download_page_view_||destroying_||hide_requested_||downloads_view_||!backend_||json.size()>32768||
       HttpOrigin(page)!=HttpOrigin(download_page_url_)||HttpOrigin(backend_->Source())!=HttpOrigin(download_page_url_)||GetDownloadPage(host_).url.empty())return;
    try {
        const auto value=ttplayer::core::JsonReader(media::Utf8(json)).Read();
        if(value["type"].String()!="ttp-sayobot")return;
        const auto url=media::Wide(value["url"].String());if(url.empty()||url.size()>8192||url.find(L'\0')!=url.npos)return;
        if(value["kind"].String()=="download") {if(!InterceptPageDownload(page,url))PageResult(E_INVALIDARG,L"");return;}
        if(value["kind"].String()!="play"||!host_.recognize_page_audio||host_.recognize_page_audio(host_.context,page.c_str(),url.c_str())!=S_OK)return;
        BrowserTrack track;track.source=url;track.title=media::Wide(value["title"].String());track.artist=media::Wide(value["artist"].String());track.tid=media::Wide(value["tid"].String());
        for(const auto* field:{&track.title,&track.artist,&track.tid})if(field->size()>1024||field->find(L'\0')!=field->npos)return;
        if(track.title.empty())return;
        Action action;action.kind=Action::Submit;action.page_action=true;action.document_generation=bridge_->document_generation;
        action.modes[1]=1;action.modes[2]=1;action.tracks.push_back(std::move(track));
        const auto queued=Queue(std::move(action));if(FAILED(queued))PageResult(queued,L"");
    }catch(...) {PageResult(E_INVALIDARG,L"");}
}
void BrowserWindow::PageResult(HRESULT result,const wchar_t* success) {
    std::wstring message=success;
    if(FAILED(result)){wchar_t code[32]{};swprintf_s(code,L"0x%08lX",static_cast<unsigned long>(result));
        message=std::wstring(L"操作失败（")+code+L"）。请检查“选项 → 网络连接 → 音乐窗 → 歌曲下载”中的保存目录。";}
    if(download_page_view_&&backend_&&SUCCEEDED(backend_->PostJson(media::Wide("{\"type\":\"ttp-sayobot-result\",\"message\":"+media::Quote(message)+"}"))))return;
    if(FAILED(result))MessageBoxW(window_,message.c_str(),L"音乐窗",MB_OK|MB_ICONERROR);
}
void BrowserWindow::SetVisible(bool visible) {
    hide_requested_ = !visible;
    if (!window_) return;
    if (!host_.set_window_visible ||
        host_.set_window_visible(host_.context, window_, visible) != S_OK)
        ShowWindow(window_, visible ? SW_SHOWNORMAL : SW_HIDE);
}
HRESULT BrowserWindow::Show(uint32_t view) {
    if (view > TTP_BROWSER_VIEW_HIDE) return E_INVALIDARG;
    if (view == TTP_BROWSER_VIEW_HIDE || (view == TTP_BROWSER_VIEW_TOGGLE && !hide_requested_ && window_ && IsWindowVisible(window_))) {
        ++bridge_->document_generation;
        SetVisible(false); StateChanged(); return S_OK;
    }
    const HRESULT hr = EnsureWindow(); if (FAILED(hr)) return hr;
    if (view == TTP_BROWSER_VIEW_MUSIC) SwitchView(false);
    else if (view == TTP_BROWSER_VIEW_DOWNLOADS) SwitchView(true);
    else SwitchView(downloads_view_);
    SetVisible(true);
    UpdateLinkTimer();
    SetForegroundWindow(window_); StateChanged(); return S_OK;
}
HRESULT BrowserWindow::ExternalDispatch(IDispatch** dispatch) {
    HRESULT hr = EnsureWindow(); if (FAILED(hr)) return hr;
    return CreateBrowserDispatch(bridge_, false, dispatch);
}
HRESULT BrowserWindow::Queue(Action action) {
    if (destroying_) return RPC_E_DISCONNECTED;
    if (actions_.size() >= 1024) return HRESULT_FROM_WIN32(ERROR_BUSY);
    HRESULT hr = EnsureWindow(); if (FAILED(hr)) return hr;
    actions_.push_back(std::move(action));
    if (!PostMessageW(window_, kActions, 0, 0)) { actions_.pop_back(); return HRESULT_FROM_WIN32(GetLastError()); }
    return S_OK;
}
void BrowserWindow::ProcessActions() {
    if (actions_.empty() || destroying_) return;
    // One action per window message leaves input responsive for large batches.
    Action action = std::move(actions_.front()); actions_.pop_front(); HRESULT hr = S_OK;
    if (action.document_generation && action.document_generation != bridge_->document_generation) return;
    if(action.kind==Action::PackageDownload){
        hr=downloads_.EnqueueFile(action.a,action.b,nullptr);
        if(SUCCEEDED(hr)){status_=L"已加入下载管理";downloads_.Refresh();}
    } else if (action.kind == Action::Submit) {
        std::vector<ttp_browser_track> tracks; tracks.reserve(action.tracks.size());
        for (const auto& track : action.tracks) tracks.push_back(track.Borrow());
        hr = host_.submit_tracks ? host_.submit_tracks(host_.context, tracks.data(), static_cast<uint32_t>(tracks.size()),
            action.modes[0], action.modes[1], action.modes[2]) : E_NOTIMPL;
    } else if (action.kind == Action::Download) {
        for (const auto& track : action.tracks) { const auto borrowed = track.Borrow(); const HRESULT item = downloads_.Enqueue(borrowed, nullptr); if (FAILED(item)) hr = item; }
        if (SUCCEEDED(hr)) { status_ = L"已加入下载管理"; downloads_.Refresh(); }
    } else if (action.kind == Action::Setup) {
        hr = host_.setup_pack ? host_.setup_pack(host_.context, action.a.c_str(), action.b.c_str(), action.c.c_str()) : E_NOTIMPL;
    } else {
        if(tooltip_)SendMessageW(tooltip_,TTM_POP,0,0);
        if(action.b.empty()) {links_.clear();link_index_=0;}
        else if(links_.size()<1024)links_.push_back({std::move(action.a),std::move(action.b)});
        else hr=HRESULT_FROM_WIN32(ERROR_BUSY);
        if(pressed_==TTP_BROWSER_LINK) {pressed_=-1;if(GetCapture()==window_)ReleaseCapture();}
        RefreshLinkHover();ResetLinkAnimation();UpdateTooltips();
    }
    if (FAILED(hr)) { wchar_t code[32]{}; swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(hr)); status_ = std::wstring(L"操作未完成：") + code; }
    if(action.page_action)PageResult(hr,action.kind==Action::PackageDownload?L"已加入下载管理":L"已加入播放列表并播放");
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}
void BrowserWindow::StateChanged() {
    if (destroying_) return;
    GetState(&state_); if (host_.state_changed) host_.state_changed(host_.context, &state_);
}
HRESULT BrowserWindow::GetState(ttp_browser_state* state) const {
    if (!state || state->size < sizeof(*state)) return E_INVALIDARG;
    *state = state_; state->size = sizeof(*state);
    if (window_) GetWindowRect(window_, &state->bounds);
    state->flags = (!hide_requested_ && window_ && IsWindowVisible(window_) ? TTP_BROWSER_STATE_VISIBLE : 0) |
        (autostart_ ? TTP_BROWSER_STATE_AUTOSTART : 0) | (downloads_view_ ? TTP_BROWSER_STATE_DOWNLOADS : 0) |
        (personal_view_ ? (media_view_->IsWebView()?TTP_BROWSER_STATE_WEBVIEW2:media_view_->IsIe()?TTP_BROWSER_STATE_IE:0) :
         browser_ready_ ? modern_attempt_ ? TTP_BROWSER_STATE_WEBVIEW2 : TTP_BROWSER_STATE_IE : 0);
    return S_OK;
}
const std::wstring& BrowserWindow::LinkText(size_t index) const {
    if(links_.empty())return kDefaultLinkText;
    const auto& link=links_[index%links_.size()];return link.text.empty()?link.url:link.text;
}
const std::wstring& BrowserWindow::LinkUrl() const {
    return links_.empty()?kDefaultLinkUrl:links_[link_index_%links_.size()].url;
}
SIZE BrowserWindow::LinkTextSize(size_t index) const {
    if(!window_)return {};
    HDC dc=GetDC(window_);if(!dc)return {};
    const auto description=LinkFont(skin_.controls[TTP_BROWSER_LINK].font);
    HFONT font=CreateFontIndirectW(&description);const auto old=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));
    const auto& text=LinkText(index);SIZE size{};GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&size);
    SelectObject(dc,old);if(font)DeleteObject(font);ReleaseDC(window_,dc);return size;
}
RECT BrowserWindow::LinkHitRect() const {
    RECT rect=ControlRect(TTP_BROWSER_LINK);if(IsRectEmpty(&rect))return rect;
    const auto size=LinkTextSize(link_index_);
    if(size.cx<rect.right-rect.left)rect.left=rect.right-size.cx;
    return rect;
}
void BrowserWindow::ResetLinkAnimation() {
    link_x_=link_y_=0;link_direction_=1;link_transition_=link_cycle_done_=false;
    const auto rect=window_?ControlRect(TTP_BROWSER_LINK):RECT{};
    link_wait_=LinkTextSize(link_index_).cx>rect.right-rect.left?25:75;
    UpdateLinkTimer();
}
void BrowserWindow::RefreshLinkHover() {
    POINT point{};bool over{};
    if(window_ && IsWindowVisible(window_) && GetCursorPos(&point) &&
       (WindowFromPoint(point)==window_ || GetCapture()==window_)) {
        ScreenToClient(window_,&point);over=Hit(point)==TTP_BROWSER_LINK;
    }
    link_hover_=over;
    if(over)hover_=TTP_BROWSER_LINK;else if(hover_==TTP_BROWSER_LINK)hover_=-1;
}
void BrowserWindow::UpdateLinkTimer() {
    if(!window_)return;
    KillTimer(window_,kLinkTimer);
    const auto rect=ControlRect(TTP_BROWSER_LINK);
    if(IsWindowVisible(window_) && !link_hover_ && pressed_!=TTP_BROWSER_LINK && !IsRectEmpty(&rect) &&
       (links_.size()>1 || LinkTextSize(link_index_).cx>rect.right-rect.left))
        SetTimer(window_,kLinkTimer,40,nullptr);
}
void BrowserWindow::TickLink() {
    if(link_hover_ || pressed_==TTP_BROWSER_LINK || !window_ || !IsWindowVisible(window_))return;
    if(link_wait_>0) {--link_wait_;return;}
    const auto rect=ControlRect(TTP_BROWSER_LINK);
    const int overflow=std::max(0L,LinkTextSize(link_index_).cx-(rect.right-rect.left));
    if(link_transition_) {
        if(++link_y_>=rect.bottom-rect.top) {
            link_index_=(link_index_+1)%links_.size();ResetLinkAnimation();UpdateTooltips();
        }
    } else if(links_.size()>1 && (!overflow || link_cycle_done_))link_transition_=true;
    else if(overflow) {
        link_x_-=link_direction_;
        if(link_x_<=-overflow){link_x_=-overflow;link_direction_=-1;link_wait_=25;}
        else if(link_x_>=0){link_x_=0;link_direction_=1;link_wait_=links_.size()>1?75:25;link_cycle_done_=true;}
    }
    InvalidateRect(window_,&rect,FALSE);
}
void BrowserWindow::PaintLink(HDC dc,RECT rect) {
    const int saved=SaveDC(dc);IntersectClipRect(dc,rect.left,rect.top,rect.right,rect.bottom);
    const auto draw=[&](size_t index,int x_offset,int y_offset) {
        const auto& text=LinkText(index);SIZE size{};GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&size);
        const int x=size.cx<rect.right-rect.left?rect.right-size.cx:rect.left;
        TextOutW(dc,x+x_offset,rect.top+(rect.bottom-rect.top-size.cy)/2+y_offset,text.c_str(),static_cast<int>(text.size()));
    };
    draw(link_index_,link_x_,-link_y_);
    if(link_transition_ && links_.size()>1)draw((link_index_+1)%links_.size(),0,rect.bottom-rect.top-link_y_);
    RestoreDC(dc,saved);
}
void BrowserWindow::BorderContextMenu(POINT point) {
    if(popup_menu_active_)return;
    if(point.x==-1 && point.y==-1) {
        point={20,20};ClientToScreen(window_,&point);
    } else {
        POINT client=point;ScreenToClient(window_,&client);
        // Only the skin background/border owns this menu. Controls and web or
        // download content retain their own mouse behavior.
        if(BackgroundHit(client)==HTCLIENT)return;
    }
    SendMessageW(window_,WM_CANCELMODE,0,0);
    RefreshDownloadPage();
    const HMENU menu=CreatePopupMenu();if(!menu)return;
    AppendMenuW(menu,MF_STRING|(downloads_view_?0:MF_CHECKED),TTP_BROWSER_MUSIC+1,L"音乐窗");
    AppendMenuW(menu,MF_STRING|(downloads_view_?MF_CHECKED:0),TTP_BROWSER_DOWNLOADS+1,L"下载管理");
    if(host_.media_source_version) {
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING|(!downloads_view_&&personal_view_?MF_CHECKED:0),5301,L"个人媒体库");
    }
    if(!download_page_name_.empty())AppendMenuW(menu,MF_STRING|(!downloads_view_&&download_page_view_?MF_CHECKED:0),5302,download_page_name_.c_str());
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|((host_.open_options||host_.open_media_settings)?0:MF_GRAYED),
        TTP_BROWSER_MENU_OPTIONS,L"选项(&P)...");
    UINT selected{};
    popup_menu_active_=true;
    const HRESULT tracked=host_.track_popup_menu
        ?host_.track_popup_menu(host_.context,window_,menu,point,&selected):E_NOTIMPL;
    if(tracked==E_NOTIMPL)
        selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window_,nullptr);
    popup_menu_active_=false;
    DestroyMenu(menu);
    if(selected==TTP_BROWSER_MENU_OPTIONS){
        const auto open=host_.open_options?host_.open_options:host_.open_media_settings;
        if(open)open(host_.context);
        return;
    }
    if(selected==5301){PersonalView(true);return;}
    if(selected==5302){DownloadPageView();return;}
    if(selected==TTP_BROWSER_MUSIC+1 || selected==TTP_BROWSER_DOWNLOADS+1)
        Activate(static_cast<int>(selected)-1);
}
void BrowserWindow::Activate(int id) {
    if (!Enabled(id)) return;
    switch (id) {
    case TTP_BROWSER_CLOSE: Show(TTP_BROWSER_VIEW_HIDE); break;
    case TTP_BROWSER_MUSIC: SwitchView(false); break;
    case TTP_BROWSER_DOWNLOADS: SwitchView(true); break;
    case TTP_BROWSER_BACK: if(personal_view_)media_view_->Back();else if (backend_) backend_->Back(); break;
    case TTP_BROWSER_FORWARD: if (backend_) backend_->Forward(); break;
    case TTP_BROWSER_REFRESH: if(personal_view_)media_view_->Refresh();else if (error_visible_) Retry(); else if (backend_) backend_->Refresh(); break;
    case TTP_BROWSER_STARTUP: autostart_ = !autostart_; StateChanged(); InvalidateRect(window_, nullptr, FALSE); break;
    case TTP_BROWSER_LINK:
        // 0040C701 delegates the link to the Shell; never navigate the embedded
        // music document or substitute a recommendation menu here.
        {const auto url=LinkUrl();if(!HttpOrigin(url).empty())ShellExecuteW(window_,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
        break;
    }
}
void BrowserWindow::Paint(HDC screen) {
    PAINTSTRUCT paint{}; const bool painting=screen==nullptr;
    if(painting)screen=BeginPaint(window_, &paint);
    RECT client{}; GetClientRect(window_, &client);
    HDC dc = CreateCompatibleDC(screen); HBITMAP buffer = CreateCompatibleBitmap(screen, std::max(1L, client.right), std::max(1L, client.bottom));
    auto old_bitmap = SelectObject(dc, buffer); auto old_font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1)); SetBkMode(dc, TRANSPARENT);
    if (skin_.valid && skin_.background) DrawBackground(dc, skin_, client.right, client.bottom);
    for (int i = 0; i < TTP_BROWSER_CONTROL_COUNT; ++i) {
        auto rect = ControlRect(i); if (IsRectEmpty(&rect)) continue;
        const auto& control = skin_.controls[i];
        struct FontSelection {
            HDC dc; HFONT font; HGDIOBJ previous;
            FontSelection(HDC device, const LOGFONTW& description) : dc(device),
                font(description.lfHeight || description.lfFaceName[0] ? CreateFontIndirectW(&description) : nullptr),
                previous(font ? SelectObject(dc,font) : nullptr) {}
            ~FontSelection() { if (font) { SelectObject(dc,previous); DeleteObject(font); } }
        } selected_font(dc, i==TTP_BROWSER_LINK?LinkFont(control.font):control.font);
        const bool selected = (i == TTP_BROWSER_MUSIC && !downloads_view_) || (i == TTP_BROWSER_DOWNLOADS && downloads_view_);
        const bool pressed = (pressed_ == i && hover_ == i) || selected;
        SetTextColor(dc, Enabled(i) ? skin_.valid ? control.color : RGB(40,55,75) : GetSysColor(COLOR_GRAYTEXT));
        if (skin_.valid && control.image && control.frames && control.image_size.cx > 0 && control.image_size.cy > 0) {
            const int width = control.image_size.cx / control.frames;
            int frame = !Enabled(i) && control.frames >= 4 ? 3 : pressed && control.frames >= 3 ? 2 : hover_ == i && control.frames >= 2 ? 1 : 0;
            if (i == TTP_BROWSER_STARTUP && autostart_ && control.frames >= 2) frame = control.frames >= 4 ? (hover_ == i ? 3 : 2) : 1;
            RECT target = rect;
            if (i == TTP_BROWSER_STARTUP) { target.right = target.left + width; target.bottom = target.top + control.image_size.cy; }
            DrawBitmap(dc, control.image, target, {width*frame,0,width*(frame+1),control.image_size.cy}, skin_.transparent_color, true);
            if (i == TTP_BROWSER_STARTUP) { rect.left = target.right + std::max(0,control.checkbox_interval); DrawTextW(dc, kLabels[i], -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS); }
            continue;
        }
        if (i == TTP_BROWSER_STARTUP) {
            RECT box{rect.left, rect.top + 2, rect.left + 14, rect.top + 16};
            DrawFrameControl(dc, &box, DFC_BUTTON, DFCS_BUTTONCHECK | (autostart_ ? DFCS_CHECKED : 0));
            rect.left += 20; DrawTextW(dc, kLabels[i], -1, &rect, DT_SINGLELINE | DT_VCENTER);
        } else if (i == TTP_BROWSER_LINK) {
            PaintLink(dc,rect);
        } else {
            if (hover_ == i || pressed) DrawEdge(dc, &rect, pressed ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
            const wchar_t* text = i == TTP_BROWSER_CLOSE ? L"×" : i == TTP_BROWSER_BACK ? L"‹" : i == TTP_BROWSER_FORWARD ? L"›" : i == TTP_BROWSER_REFRESH ? L"↻" : kLabels[i];
            DrawTextW(dc, text, -1, &rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
    }
    BitBlt(screen, 0,0,client.right,client.bottom,dc,0,0,SRCCOPY);
    SelectObject(dc, old_font); SelectObject(dc, old_bitmap); DeleteObject(buffer); DeleteDC(dc);
    if(painting)EndPaint(window_, &paint);
}
BOOL BrowserWindow::Translate(MSG* message) {
    if (!message || !window_ || !IsWindowVisible(window_) ||
        (message->hwnd != window_ && !IsChild(window_, message->hwnd))) return FALSE;
    if (message->message == WM_KEYDOWN) {
        if(message->wParam==VK_ESCAPE && downloads_view_ && downloads_.Window()) {
            const HWND captured=GetCapture();
            if(captured && (captured==downloads_.Window() || IsChild(downloads_.Window(),captured))) {
                SendMessageW(captured,WM_CANCELMODE,0,0);return TRUE;
            }
        }
        if (message->wParam==VK_ESCAPE && drag_hit_!=HTNOWHERE) { EndBackgroundDrag(true);return TRUE; }
        if (message->wParam == VK_ESCAPE || message->wParam == VK_F11) { Show(TTP_BROWSER_VIEW_HIDE); return TRUE; }
        if (message->wParam == VK_F5 && !downloads_view_) { Activate(TTP_BROWSER_REFRESH); return TRUE; }
    }
    if(!downloads_view_ && personal_view_)return media_view_->Translate(message);
    if (!downloads_view_ && !error_visible_ && backend_ && backend_->Translate(message)) return TRUE;
    return error_visible_ ? IsDialogMessageW(window_, message) : FALSE;
}
LRESULT CALLBACK BrowserWindow::Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<BrowserWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) { self = static_cast<BrowserWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->window_ = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
    if (!self) return DefWindowProcW(hwnd, message, wp, lp);
    try { switch (message) {
    case WM_NCCALCSIZE: return 0;
    case WM_NCPAINT: return 0;
    case WM_NCACTIVATE: return TRUE;
    case WM_THEMECHANGED:
    case 0x031e: // WM_DWMCOMPOSITIONCHANGED; keep legacy SDK compatibility.
        self->ApplyFramePolicy();
        RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_NOERASE);return 0;
    case WM_CREATE:
        self->ApplyFramePolicy(); self->UpdateWindowRegion();
        self->tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
            0,0,0,0,hwnd,nullptr,self->instance_,nullptr);
        self->error_panel_ = CreateWindowExW(0,L"STATIC",L"",WS_CHILD | SS_WHITERECT,0,0,0,0,hwnd,nullptr,self->instance_,nullptr);
        self->error_text_ = CreateWindowExW(0,L"STATIC",L"",WS_CHILD | SS_CENTER,0,0,0,0,hwnd,nullptr,self->instance_,nullptr);
        self->retry_button_ = CreateWindowExW(0,L"BUTTON",L"重新加载",WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,0,0,0,0,hwnd,reinterpret_cast<HMENU>(kRetry),self->instance_,nullptr);
        SendMessageW(self->error_text_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageW(self->retry_button_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        self->Layout(); return 0;
    case WM_SIZE: self->UpdateWindowRegion();self->Layout();
        RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_NOERASE);return 0;
    case WM_EXITSIZEMOVE: self->StateChanged(); return 0;
    case WM_SHOWWINDOW: if (!wp) { self->EndBackgroundDrag();self->link_hover_=false;KillTimer(hwnd,kLinkTimer);
        if (self->pressed_>=0) { self->pressed_=-1;ReleaseCapture(); } } else self->ResetLinkAnimation();break;
    case WM_CLOSE: self->Show(TTP_BROWSER_VIEW_HIDE); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: self->Paint(); return 0;
    case WM_PRINTCLIENT: if(wp)self->Paint(reinterpret_cast<HDC>(wp));return 0;
    case WM_GETMINMAXINFO: { auto* limits=reinterpret_cast<MINMAXINFO*>(lp);const auto minimum=self->MinimumSize();
        limits->ptMinTrackSize={minimum.cx,minimum.cy};return 0; }
    case WM_NCHITTEST: return HTCLIENT;
    case WM_SETCURSOR: {
        if (reinterpret_cast<HWND>(wp)!=hwnd || LOWORD(lp)!=HTCLIENT) break;
        POINT point{};GetCursorPos(&point);ScreenToClient(hwnd,&point);
        const int hit=self->drag_hit_!=HTNOWHERE ? self->drag_hit_ : self->BackgroundHit(point);
        const int control=self->Hit(point);
        LPCWSTR cursor=control>=0 && self->Enabled(control)?IDC_HAND:IDC_ARROW;
        if(hit==HTTOPLEFT || hit==HTBOTTOMRIGHT)cursor=IDC_SIZENWSE;
        else if(hit==HTTOPRIGHT || hit==HTBOTTOMLEFT)cursor=IDC_SIZENESW;
        else if(hit==HTLEFT || hit==HTRIGHT)cursor=IDC_SIZEWE;
        else if(hit==HTTOP || hit==HTBOTTOM)cursor=IDC_SIZENS;
        SetCursor(LoadCursorW(nullptr,cursor));return TRUE;
    }
    case WM_MOUSEMOVE: {
        if(self->drag_hit_!=HTNOWHERE) { self->ContinueBackgroundDrag();return 0; }
        POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; const int hit = self->Hit(point);
        const bool link_hover=hit==TTP_BROWSER_LINK;
        if(link_hover!=self->link_hover_) {self->link_hover_=link_hover;self->ResetLinkAnimation();self->UpdateTooltips();}
        if (hit != self->hover_) { self->hover_ = hit; InvalidateRect(hwnd,nullptr,FALSE); }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&tracking); return 0;
    }
    case WM_MOUSELEAVE: self->hover_ = -1;
        if(self->link_hover_){self->link_hover_=false;self->ResetLinkAnimation();}
        InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case WM_LBUTTONDOWN: { POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; self->pressed_ = self->Hit(point);
        if(self->pressed_==TTP_BROWSER_LINK){self->link_hover_=true;self->ResetLinkAnimation();}
        if (self->pressed_ >= 0) { self->hover_ = self->pressed_; SetCapture(hwnd); InvalidateRect(hwnd,nullptr,FALSE); }
        else self->BeginBackgroundDrag(point);return 0; }
    case WM_LBUTTONUP: { if(self->drag_hit_!=HTNOWHERE) { self->EndBackgroundDrag();return 0; }
        const int pressed = self->pressed_; self->pressed_ = -1; ReleaseCapture(); POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if (pressed >= 0 && pressed == self->Hit(point)) self->Activate(pressed); InvalidateRect(hwnd,nullptr,FALSE); return 0; }
    case WM_CAPTURECHANGED: self->EndBackgroundDrag();self->pressed_ = -1;self->UpdateLinkTimer(); InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case WM_CANCELMODE: self->EndBackgroundDrag(true);self->pressed_=-1;if(GetCapture()==hwnd)ReleaseCapture();self->UpdateLinkTimer();break;
    case WM_CONTEXTMENU:
        if(reinterpret_cast<HWND>(wp)==hwnd)self->BorderContextMenu({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
        return 0;
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_MENUCHAR:
    case WM_INITMENUPOPUP:
    case WM_MENUSELECT:
    case WM_EXITMENULOOP:
        // Keep this window as the popup owner while the host owns the shared
        // menu images, measurement and drawing, as with the lyrics window.
        if(self->popup_menu_active_ && self->host_.track_popup_menu && IsWindow(self->host_.owner)) {
            if(message==WM_DRAWITEM && (!lp || reinterpret_cast<DRAWITEMSTRUCT*>(lp)->CtlType!=ODT_MENU))break;
            if(message==WM_MEASUREITEM && (!lp || reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->CtlType!=ODT_MENU))break;
            return SendMessageW(self->host_.owner,message,wp,lp);
        }
        break;
    case WM_NOTIFY: {
        const auto* header=reinterpret_cast<NMHDR*>(lp);
        if(header && header->hwndFrom==self->tooltip_ && header->code==TTN_GETDISPINFOW && header->idFrom==TTP_BROWSER_LINK+1) {
            auto* tip=reinterpret_cast<NMTTDISPINFOW*>(lp);
            tip->lpszText=const_cast<wchar_t*>(self->LinkText(self->link_index_).c_str());return 0;
        }
        break;
    }
    case WM_COMMAND: if (LOWORD(wp) == kRetry) { self->Retry(); return 0; } break;
    case WM_TIMER: if(wp==kLinkTimer){self->TickLink();return 0;} if (wp == kNavigationTimer) {
        KillTimer(hwnd,kNavigationTimer);
        if (self->modern_attempt_ && !self->browser_ready_ && self->engine_ == 0) PostMessageW(hwnd,kFallback,0,0);
        else { if (self->backend_) self->backend_->Stop(); self->Error(L"网页在 30 秒内没有完成加载，请检查连接后重试。"); }
        return 0;
    } break;
    case kActions: self->ProcessActions(); return 0;
    case kOpenDownloadPage: self->DownloadPageView(); return 0;
    case kFallback: if (!self->destroying_ && !self->personal_view_ && !self->browser_ready_) self->StartBrowser(true); return 0;
    case WM_DESTROY:
        self->destroying_ = true; self->bridge_->alive = false; self->actions_.clear();
        KillTimer(hwnd,kNavigationTimer);KillTimer(hwnd,kLinkTimer); if (self->backend_) self->backend_->Shutdown();
        if (self->tooltip_) { DestroyWindow(self->tooltip_); self->tooltip_ = nullptr; } return 0;
    case WM_NCDESTROY: SetWindowLongPtrW(hwnd,GWLP_USERDATA,0); self->window_ = nullptr; break;
    } } catch (...) { return message == WM_CREATE ? -1 : 0; }
    return DefWindowProcW(hwnd,message,wp,lp);
}
}

void ttpbrowser::BrowserWindow::MediaSettingsChanged(){if(media_view_)media_view_->ReloadProfiles();}
