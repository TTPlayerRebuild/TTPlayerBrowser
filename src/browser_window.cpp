#include "browser_window.h"
#include "downloads.h"
#include <commctrl.h>
#include <windowsx.h>
#include <shlobj.h>
#include <algorithm>
#include <cstring>
#include <new>

namespace ttpbrowser {
namespace {
constexpr wchar_t kClass[] = L"TTPlayer_BrowserWnd";
constexpr UINT kActions = WM_APP + 0x321, kFallback = WM_APP + 0x322;
constexpr UINT_PTR kNavigationTimer = 101;
constexpr int kRetry = 2001;
const wchar_t* const kLabels[]{L"关闭", L"音乐窗", L"下载管理", L"后退", L"前进", L"刷新", L"启动时显示", L"千千音乐"};
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
BrowserWindow::BrowserWindow(const ttp_browser_host& host, const ttp_browser_config& config, Downloads& downloads)
    : host_(host), downloads_(downloads), bridge_(std::make_shared<BridgeContext>()) {
    HMODULE module{}; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&BrowserWindow::Proc), &module); instance_ = module;
    state_ = config.initial_state; state_.size = sizeof(state_);
    first_position_ = !ValidBounds(config.initial_state.bounds);
    if (!ValidBounds(state_.bounds)) state_.bounds = {140, 100, 700, 640};
    autostart_ = (state_.flags & TTP_BROWSER_STATE_AUTOSTART) != 0;
    downloads_view_ = (state_.flags & TTP_BROWSER_STATE_DOWNLOADS) != 0;
    bridge_->allowed = [this] {
        return !destroying_ && window_ && IsWindowVisible(window_) && !downloads_view_ && browser_ready_ && backend_ &&
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
    bridge_->recommendations_changed = [this] { if (window_) InvalidateRect(window_, nullptr, FALSE); };
    Configure(config);
}
BrowserWindow::~BrowserWindow() {
    destroying_ = true;
    bridge_->alive = false; bridge_->allowed = {}; bridge_->submit = {}; bridge_->query = {}; bridge_->setup = {};
    bridge_->hyperlink = {}; bridge_->recommendations_changed = {}; bridge_->recommendations.clear();
    actions_.clear();
    if (backend_) backend_->Shutdown(); backend_.reset(); external_.Reset();
    if (window_) { KillTimer(window_, kNavigationTimer); DestroyWindow(window_); }
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
    bridge_->accept_recommendations = config.accept_recommendations != FALSE;
    if (!bridge_->accept_recommendations) bridge_->recommendations.clear();
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
    if (window_) {
        SetWindowPos(window_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetLayeredWindowAttributes(window_, skin_.valid ? skin_.transparent_color : RGB(255,0,255), opacity_, LWA_COLORKEY | LWA_ALPHA);
        if (changed_engine && backend_) {
            backend_->Shutdown(); backend_.reset(); browser_ready_ = false;
            if (IsWindowVisible(window_) && !downloads_view_) StartBrowser();
        } else if (changed && backend_) backend_->Navigate(home_url_);
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
            skin_.valid ? std::max(240L,skin_.background_size.cy) : 240};
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
    if (id == TTP_BROWSER_BACK) return !downloads_view_ && can_back_;
    if (id == TTP_BROWSER_FORWARD) return !downloads_view_ && can_forward_;
    if (id == TTP_BROWSER_REFRESH) return !downloads_view_;
    return true;
}
int BrowserWindow::Hit(POINT point) const {
    for (int i = 0; i < TTP_BROWSER_CONTROL_COUNT; ++i) { const auto bounds = ControlRect(i); if (PtInRect(&bounds, point)) return i; }
    return -1;
}
void BrowserWindow::UpdateTooltips() {
    if (!tooltip_) return;
    for (int i = 0; i < TTP_BROWSER_CONTROL_COUNT; ++i) {
        TOOLINFOW tool{sizeof(tool)}; tool.hwnd = window_; tool.uId = i + 1; tool.rect = ControlRect(i);
        // GETTOOLINFO writes text into lpszText when non-null. Query only the
        // registration here; never pass the read-only label as an output buffer.
        // ADDTOOL below borrows that label as input, which is a different contract.
        if (!SendMessageW(tooltip_, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&tool))) {
            tool.hwnd = window_; tool.uId = i + 1; tool.rect = ControlRect(i); tool.uFlags = TTF_SUBCLASS; tool.lpszText = const_cast<wchar_t*>(kLabels[i]);
            SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        } else { tool.rect = ControlRect(i); SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool)); }
    }
}
void BrowserWindow::Layout() {
    if (!window_) return;
    const auto bounds = ContentRect(); if (backend_) backend_->Resize(bounds);
    if (downloads_.Window()) downloads_.Resize(bounds);
    if (error_panel_) {
        MoveWindow(error_panel_, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, TRUE);
        const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        // Error controls are children of the main HWND so the retry command is
        // delivered to our WndProc, while their positions overlay the panel.
        MoveWindow(error_text_, bounds.left + 22, bounds.top + std::max(16, height / 2 - 70), std::max(1, width - 44), 120, TRUE);
        MoveWindow(retry_button_, bounds.left + std::max(12, width / 2 - 55), bounds.top + std::min(height - 38, height / 2 + 62), 110, 28, TRUE);
    }
    UpdateTooltips();
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
    events.trusted = [this](const std::wstring& target) { return !origin_.empty() && HttpOrigin(target) == origin_; };
    events.download = [this](const std::wstring& source) {
        Action action; action.kind = Action::Download; BrowserTrack track; track.source = source;
        action.tracks.push_back(std::move(track)); Queue(std::move(action));
    };
    const HRESULT hr = backend_->Start(window_, ContentRect(), external_.Get(), profile_, std::move(events));
    if (FAILED(hr)) {
        const bool retry_ie = modern_attempt_ && !force_ie && engine_ == 0; backend_->Shutdown(); backend_.reset();
        if (retry_ie) return StartBrowser(true);
        Error(L"无法创建网页浏览器。请检查系统浏览器组件或 WebView2 Runtime。"); return hr;
    }
    backend_->Visible(!downloads_view_); loading_ = true;
    SetTimer(window_, kNavigationTimer, 30000, nullptr);
    return backend_->Navigate(current_url_.empty() ? home_url_ : current_url_);
}
void BrowserWindow::NavigationStarting(const std::wstring& target) {
    ++bridge_->document_generation;
    current_url_ = target; loading_ = true; error_visible_ = false; status_ = L"正在打开音乐窗…";
    ShowWindow(error_panel_, SW_HIDE); ShowWindow(error_text_, SW_HIDE); ShowWindow(retry_button_, SW_HIDE);
    if (backend_) backend_->Visible(!downloads_view_);
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
    if (backend_) backend_->Visible(!downloads_view_); InvalidateRect(window_, nullptr, FALSE);
}
void BrowserWindow::Error(const std::wstring& message) {
    if (!window_) return;
    KillTimer(window_, kNavigationTimer); loading_ = false; error_visible_ = true; status_ = message;
    const std::wstring text = L"音乐窗暂时无法打开\r\n\r\n" + message + L"\r\n" + (current_url_.empty() ? home_url_ : current_url_);
    SetWindowTextW(error_text_, text.c_str());
    if (backend_) backend_->Visible(false);
    ShowWindow(error_panel_, downloads_view_ ? SW_HIDE : SW_SHOW);
    ShowWindow(error_text_, downloads_view_ ? SW_HIDE : SW_SHOW);
    ShowWindow(retry_button_, downloads_view_ ? SW_HIDE : SW_SHOW);
    if (!downloads_view_) { BringWindowToTop(error_panel_); BringWindowToTop(error_text_); BringWindowToTop(retry_button_); }
    InvalidateRect(window_, nullptr, FALSE);
}
void BrowserWindow::Retry() {
    if (!backend_ || !browser_ready_) { StartBrowser(); return; }
    const HRESULT hr = backend_->Navigate(current_url_.empty() ? home_url_ : current_url_);
    if (FAILED(hr)) Error(L"无法重新打开音乐网页。");
}
void BrowserWindow::SwitchView(bool downloads_view) {
    downloads_view_ = downloads_view;
    if (downloads_view_) {
        if (!downloads_.Window()) downloads_.CreateView(window_);
        if (downloads_.Window()) { downloads_.Resize(ContentRect()); downloads_.Refresh(); ShowWindow(downloads_.Window(), SW_SHOW); }
        if (backend_) backend_->Visible(false);
        ShowWindow(error_panel_, SW_HIDE); ShowWindow(error_text_, SW_HIDE); ShowWindow(retry_button_, SW_HIDE);
    } else {
        if (downloads_.Window()) ShowWindow(downloads_.Window(), SW_HIDE);
        if (!backend_) StartBrowser();
        else if (error_visible_) Error(status_);
        else backend_->Visible(true);
    }
    InvalidateRect(window_, nullptr, FALSE); StateChanged();
}
HRESULT BrowserWindow::Show(uint32_t view) {
    if (view > TTP_BROWSER_VIEW_HIDE) return E_INVALIDARG;
    if (view == TTP_BROWSER_VIEW_HIDE || (view == TTP_BROWSER_VIEW_TOGGLE && window_ && IsWindowVisible(window_))) {
        ++bridge_->document_generation;
        if (window_) ShowWindow(window_, SW_HIDE); StateChanged(); return S_OK;
    }
    const HRESULT hr = EnsureWindow(); if (FAILED(hr)) return hr;
    ShowWindow(window_, SW_SHOWNORMAL);
    if (view == TTP_BROWSER_VIEW_MUSIC) SwitchView(false);
    else if (view == TTP_BROWSER_VIEW_DOWNLOADS) SwitchView(true);
    else SwitchView(downloads_view_);
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
    if (action.kind == Action::Submit) {
        std::vector<ttp_browser_track> tracks; tracks.reserve(action.tracks.size());
        for (const auto& track : action.tracks) tracks.push_back(track.Borrow());
        hr = host_.submit_tracks ? host_.submit_tracks(host_.context, tracks.data(), static_cast<uint32_t>(tracks.size()),
            action.modes[0], action.modes[1], action.modes[2]) : E_NOTIMPL;
    } else if (action.kind == Action::Download) {
        for (const auto& track : action.tracks) { const auto borrowed = track.Borrow(); const HRESULT item = downloads_.Enqueue(borrowed, nullptr); if (FAILED(item)) hr = item; }
        if (SUCCEEDED(hr)) { status_ = L"已加入下载管理"; downloads_.Refresh(); }
    } else if (action.kind == Action::Setup) {
        hr = host_.setup_pack ? host_.setup_pack(host_.context, action.a.c_str(), action.b.c_str(), action.c.c_str()) : E_NOTIMPL;
    } else { link_text_ = std::move(action.a); link_url_ = std::move(action.b); }
    if (FAILED(hr)) { wchar_t code[32]{}; swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(hr)); status_ = std::wstring(L"操作未完成：") + code; }
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
    state->flags = (window_ && IsWindowVisible(window_) ? TTP_BROWSER_STATE_VISIBLE : 0) |
        (autostart_ ? TTP_BROWSER_STATE_AUTOSTART : 0) | (downloads_view_ ? TTP_BROWSER_STATE_DOWNLOADS : 0) |
        (browser_ready_ ? modern_attempt_ ? TTP_BROWSER_STATE_WEBVIEW2 : TTP_BROWSER_STATE_IE : 0);
    return S_OK;
}
void BrowserWindow::Recommendations() {
    if (bridge_->recommendations.empty()) { if (!link_url_.empty() && backend_) backend_->Navigate(link_url_); return; }
    HMENU menu = CreatePopupMenu();
    const size_t count = std::min<size_t>(bridge_->recommendations.size(), 500);
    for (size_t i = 0; i < count; ++i) {
        const auto& track = bridge_->recommendations[i];
        std::wstring title = track.artist.empty() ? track.title : track.artist + L" - " + track.title;
        if (title.empty()) title = track.source; AppendMenuW(menu, MF_STRING, i + 1, title.c_str());
    }
    POINT point{}; GetCursorPos(&point); const UINT selected = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr));
    DestroyMenu(menu);
    if (selected && selected <= count) { Action action; action.kind = Action::Submit;
        action.modes[1] = action.modes[2] = 1; action.tracks.push_back(bridge_->recommendations[selected - 1]); Queue(std::move(action)); }
}
void BrowserWindow::Activate(int id) {
    if (!Enabled(id)) return;
    switch (id) {
    case TTP_BROWSER_CLOSE: Show(TTP_BROWSER_VIEW_HIDE); break;
    case TTP_BROWSER_MUSIC: SwitchView(false); break;
    case TTP_BROWSER_DOWNLOADS: SwitchView(true); break;
    case TTP_BROWSER_BACK: if (backend_) backend_->Back(); break;
    case TTP_BROWSER_FORWARD: if (backend_) backend_->Forward(); break;
    case TTP_BROWSER_REFRESH: if (error_visible_) Retry(); else if (backend_) backend_->Refresh(); break;
    case TTP_BROWSER_STARTUP: autostart_ = !autostart_; StateChanged(); InvalidateRect(window_, nullptr, FALSE); break;
    case TTP_BROWSER_LINK:
        if (!link_url_.empty()) { SwitchView(false); if (backend_) backend_->Navigate(link_url_); }
        else if (!bridge_->recommendations.empty()) Recommendations();
        else { SwitchView(false); if (backend_) backend_->Navigate(home_url_); }
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
        } selected_font(dc, control.font);
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
            const auto text = !link_text_.empty() ? link_text_ : !bridge_->recommendations.empty() ?
                L"推荐歌曲（" + std::to_wstring(bridge_->recommendations.size()) + L"）" : status_.empty() ? std::wstring(L"千千音乐") : status_;
            DrawTextW(dc, text.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS);
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
        if (message->wParam==VK_ESCAPE && drag_hit_!=HTNOWHERE) { EndBackgroundDrag(true);return TRUE; }
        if (message->wParam == VK_ESCAPE || message->wParam == VK_F11) { Show(TTP_BROWSER_VIEW_HIDE); return TRUE; }
        if (message->wParam == VK_F5 && !downloads_view_) { Activate(TTP_BROWSER_REFRESH); return TRUE; }
    }
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
    case WM_SHOWWINDOW: if (!wp) { self->EndBackgroundDrag(); if (self->pressed_>=0) { self->pressed_=-1;ReleaseCapture(); } } break;
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
        LPCWSTR cursor=IDC_ARROW;
        if(hit==HTTOPLEFT || hit==HTBOTTOMRIGHT)cursor=IDC_SIZENWSE;
        else if(hit==HTTOPRIGHT || hit==HTBOTTOMLEFT)cursor=IDC_SIZENESW;
        else if(hit==HTLEFT || hit==HTRIGHT)cursor=IDC_SIZEWE;
        else if(hit==HTTOP || hit==HTBOTTOM)cursor=IDC_SIZENS;
        SetCursor(LoadCursorW(nullptr,cursor));return TRUE;
    }
    case WM_MOUSEMOVE: {
        if(self->drag_hit_!=HTNOWHERE) { self->ContinueBackgroundDrag();return 0; }
        POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; const int hit = self->Hit(point);
        if (hit != self->hover_) { self->hover_ = hit; InvalidateRect(hwnd,nullptr,FALSE); }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&tracking); return 0;
    }
    case WM_MOUSELEAVE: self->hover_ = -1; InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case WM_LBUTTONDOWN: { POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; self->pressed_ = self->Hit(point);
        if (self->pressed_ >= 0) { self->hover_ = self->pressed_; SetCapture(hwnd); InvalidateRect(hwnd,nullptr,FALSE); }
        else self->BeginBackgroundDrag(point);return 0; }
    case WM_LBUTTONUP: { if(self->drag_hit_!=HTNOWHERE) { self->EndBackgroundDrag();return 0; }
        const int pressed = self->pressed_; self->pressed_ = -1; ReleaseCapture(); POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if (pressed >= 0 && pressed == self->Hit(point)) self->Activate(pressed); InvalidateRect(hwnd,nullptr,FALSE); return 0; }
    case WM_CAPTURECHANGED: self->EndBackgroundDrag();self->pressed_ = -1; InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case WM_CANCELMODE: self->EndBackgroundDrag(true);self->pressed_=-1;if(GetCapture()==hwnd)ReleaseCapture();return 0;
    case WM_CONTEXTMENU: self->Recommendations(); return 0;
    case WM_COMMAND: if (LOWORD(wp) == kRetry) { self->Retry(); return 0; } break;
    case WM_TIMER: if (wp == kNavigationTimer) {
        KillTimer(hwnd,kNavigationTimer);
        if (self->modern_attempt_ && !self->browser_ready_ && self->engine_ == 0) PostMessageW(hwnd,kFallback,0,0);
        else { if (self->backend_) self->backend_->Stop(); self->Error(L"网页在 30 秒内没有完成加载，请检查连接后重试。"); }
        return 0;
    } break;
    case kActions: self->ProcessActions(); return 0;
    case kFallback: if (!self->destroying_ && !self->browser_ready_) self->StartBrowser(true); return 0;
    case WM_DESTROY:
        self->destroying_ = true; self->bridge_->alive = false; self->actions_.clear();
        KillTimer(hwnd,kNavigationTimer); if (self->backend_) self->backend_->Shutdown();
        if (self->tooltip_) { DestroyWindow(self->tooltip_); self->tooltip_ = nullptr; } return 0;
    case WM_NCDESTROY: SetWindowLongPtrW(hwnd,GWLP_USERDATA,0); self->window_ = nullptr; break;
    } } catch (...) { return message == WM_CREATE ? -1 : 0; }
    return DefWindowProcW(hwnd,message,wp,lp);
}
}
