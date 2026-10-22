#include "ttpbrowser.h"
#include "skin_render_probe.h"
#include <commctrl.h>
#include <shlwapi.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
int failures{};
void Check(bool ok,const char* text) {std::printf("%s %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);if(!ok)++failures;}
void Pump(DWORD duration=150) {
    const DWORD start=GetTickCount();
    do {MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(5);}
    while(GetTickCount()-start<duration);
}
struct Song {std::wstring source,artist,title,tid;};
struct Host {
    std::vector<Song> submitted;
    int mode[3]{};
    unsigned submits{};
    std::atomic_uint opens{};
    std::vector<unsigned char> bytes=std::vector<unsigned char>(131072,0x5a);
    ttp_browser_state last{};
};
HRESULT WINAPI Submit(void* p,const ttp_browser_track* tracks,uint32_t n,int a,int b,int c) {
    auto& h=*static_cast<Host*>(p);++h.submits;h.mode[0]=a;h.mode[1]=b;h.mode[2]=c;
    for(uint32_t i=0;i<n;++i)h.submitted.push_back({tracks[i].source?tracks[i].source:L"",
        tracks[i].artist?tracks[i].artist:L"",tracks[i].title?tracks[i].title:L"",tracks[i].tid?tracks[i].tid:L""});
    return S_OK;
}
HRESULT WINAPI Query(void*,uint32_t kind,uint32_t selector,BSTR* out) {
    if(!out)return E_POINTER;*out=SysAllocString(kind==1?(selector==1?L"5.7.9":L"0"):
        (selector==1?L"Fixture skin":L"Fixture author"));return *out?S_OK:E_OUTOFMEMORY;
}
HRESULT WINAPI Open(void* p,const wchar_t* url,HANDLE canceled,IStream** out,BOOL* persist) {
    if(!out || !persist || !url)return E_INVALIDARG;*out=nullptr;*persist=TRUE;
    auto& h=*static_cast<Host*>(p);++h.opens;
    if(WaitForSingleObject(canceled,0)==WAIT_OBJECT_0)return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    IStream* stream{};HRESULT hr=CreateStreamOnHGlobal(nullptr,TRUE,&stream);if(FAILED(hr))return hr;
    ULONG done{};hr=stream->Write(h.bytes.data(),static_cast<ULONG>(h.bytes.size()),&done);
    LARGE_INTEGER zero{};stream->Seek(zero,STREAM_SEEK_SET,nullptr);
    if(FAILED(hr)){stream->Release();return hr;}*out=stream;return S_OK;
}
void WINAPI Changed(void* p,const ttp_browser_state* s){if(s)static_cast<Host*>(p)->last=*s;}
HRESULT WINAPI Setup(void*,const wchar_t*,const wchar_t*,const wchar_t*) {return E_NOTIMPL;}
VARIANT Text(const wchar_t* value){VARIANT v{};v.vt=VT_BSTR;v.bstrVal=SysAllocString(value);return v;}
VARIANT Number(short value){VARIANT v{};v.vt=VT_I2;v.iVal=value;return v;}
HRESULT Invoke(IDispatch* object,const wchar_t* name,std::vector<VARIANT> arguments={},VARIANT* result=nullptr,WORD flags=DISPATCH_METHOD) {
    LPOLESTR n=const_cast<LPOLESTR>(name);DISPID id{};
    HRESULT hr=object->GetIDsOfNames(IID_NULL,&n,1,LOCALE_USER_DEFAULT,&id);
    std::reverse(arguments.begin(),arguments.end());DISPPARAMS args{};
    args.rgvarg=arguments.data();args.cArgs=static_cast<UINT>(arguments.size());
    if(SUCCEEDED(hr))hr=object->Invoke(id,IID_NULL,LOCALE_USER_DEFAULT,flags,&args,result,nullptr,nullptr);
    for(auto& a:arguments)VariantClear(&a);return hr;
}
void CheckBackgroundPatches(const ttp_browser_api* api,void* instance) {
    constexpr int width=320,height=240;
    const HWND window=api->window(instance);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* raw{};HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&raw,nullptr,0);
    Check(bitmap && raw,"allocate pixel-identified background fixture");
    if(!bitmap || !raw){if(bitmap)DeleteObject(bitmap);return;}
    auto* pixels=static_cast<DWORD*>(raw);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        pixels[y*width+x]=((x%251+1)<<16)|((y%251+1)<<8)|0x55;
    ttp_browser_skin skin{};skin.size=sizeof(skin);skin.valid=TRUE;
    skin.background=bitmap;skin.background_size={width,height};
    skin.transparent_color=RGB(255,0,255);skin.content={12,32,308,214};
    const auto verify=[&](SIZE destination,RECT patch,bool tile,const char* label){
        SetWindowPos(window,nullptr,0,0,destination.cx,destination.cy,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        const auto rendered=ttpbrowser::testing::RenderClient(window);
        const auto sample=[&](int position,int total,int original,int low,int high){
            if(position<low)return position;
            if(position>=total-(original-high))return position-(total-original);
            return tile?low+(position-low)%(high-low):low;
        };
        bool same=rendered.pixels.size()==static_cast<size_t>(destination.cx)*destination.cy;
        for(int y=0;same && y<destination.cy;++y)for(int x=0;x<destination.cx;++x){
            const int sx=sample(x,destination.cx,width,patch.left,patch.right);
            const int sy=sample(y,destination.cy,height,patch.top,patch.bottom);
            if((rendered.pixels[y*destination.cx+x]&0xffffff)!=pixels[sy*width+sx]){same=false;break;}
        }
        Check(same,label);
    };
    Check(SUCCEEDED(api->set_skin(instance,&skin)),"install legacy skin without resize_rect");
    for(const SIZE destination : {SIZE{480,360},SIZE{581,443},SIZE{320,360},SIZE{480,240},SIZE{320,240}})
        verify(destination,{160,120,161,121},false,"implicit nine-patch preserves every fixed pixel across horizontal/vertical resizing");
    skin.resize_rect={width+4,height+4,width+8,height+8};
    Check(SUCCEEDED(api->set_skin(instance,&skin)),"install out-of-image resize rectangle");
    verify({480,360},{160,120,161,121},false,"invalid resize rectangle uses bounded center-band fallback");
    skin.resize_rect={17,29,291,211};skin.resize_tile=TRUE;
    Check(SUCCEEDED(api->set_skin(instance,&skin)),"install explicit tiled nine-patch");
    verify({487,363},skin.resize_rect,true,"explicit tile repeats native pixels and clips partial final tiles");
    // Solid stretch cells make the expected result independent of GDI's
    // nearest-neighbour sampling convention; corners keep unique pixel IDs.
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const int column=x<17?0:x<291?1:2,row=y<29?0:y<211?1:2;
        if(column==1 || row==1)pixels[y*width+x]=0x105020+column*0x200000+row*0x003000;
    }
    skin.resize_tile=FALSE;
    Check(SUCCEEDED(api->set_skin(instance,&skin)),"install explicit stretched nine-patch");
    verify({487,363},skin.resize_rect,false,"explicit stretch preserves corners and expands only selected bands");
    verify({300,220},skin.resize_rect,false,"size below source bitmap still uses fixed nine-patch borders");
    DeleteObject(bitmap);
}
void CheckBorderlessWindow(const ttp_browser_api* api,void* instance,Host& host) {
    const HWND window=api->window(instance);
    const LONG_PTR style=GetWindowLongPtrW(window,GWL_STYLE);
    Check((style&WS_POPUP) && !(style&(WS_THICKFRAME|WS_CAPTION|WS_BORDER)),
        "music window is a skin popup without a system frame or caption");
    RECT outer{},client{};GetWindowRect(window,&outer);GetClientRect(window,&client);
    POINT origin{};ClientToScreen(window,&origin);
    Check(client.right==outer.right-outer.left && client.bottom==outer.bottom-outer.top &&
        origin.x==outer.left && origin.y==outer.top,"skin client covers the exact outer HWND rectangle");

    // A frame-only fixture leaves both its four corners and content center
    // transparent. The HWND must cut out the corners but keep the embedded
    // browser/download surface opaque; testing color key flags alone misses it.
    constexpr int width=320,height=240,cutout=10;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels{};HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    Check(bitmap && pixels,"create skin fixture with transparent corners and content");
    if(!bitmap || !pixels){if(bitmap)DeleteObject(bitmap);return;}
    auto* data=static_cast<DWORD*>(pixels);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const bool corner=(x<cutout || x>=width-cutout) && (y<cutout || y>=height-cutout);
        const bool content=x>=12 && x<308 && y>=32 && y<214;
        data[y*width+x]=corner || content ? 0x00ff00ff : 0x00285080;
    }
    ttp_browser_skin skin{};skin.size=sizeof(skin);skin.valid=TRUE;skin.position={0,0,width,height};
    skin.background=bitmap;skin.background_size={width,height};skin.transparent_color=RGB(255,0,255);
    skin.content={12,32,308,214};skin.resize_rect={16,40,304,208};
    Check(SUCCEEDED(api->set_skin(instance,&skin)),"install copied frame-only skin fixture");
    DeleteObject(bitmap); // Exercise the DLL's ownership contract as well.
    auto verify_region=[&](const char* label){
        RECT bounds{};GetClientRect(window,&bounds);HRGN region=CreateRectRgn(0,0,0,0);
        const int kind=GetWindowRgn(window,region);
        const bool ok=kind!=ERROR && !PtInRegion(region,1,1) && !PtInRegion(region,bounds.right-2,1) &&
            !PtInRegion(region,1,bounds.bottom-2) && !PtInRegion(region,bounds.right-2,bounds.bottom-2) &&
            PtInRegion(region,2,12) && PtInRegion(region,bounds.right/2,bounds.bottom/2);
        DeleteObject(region);Check(ok,label);
    };
    SetWindowPos(window,nullptr,outer.left,outer.top,480,360,SWP_NOZORDER|SWP_NOACTIVATE);
    verify_region("skin region removes all transparent corners and includes declared content");
    SetWindowPos(window,nullptr,0,0,580,440,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    verify_region("resized nine-patch region preserves transparent corners and opaque child content");
    Check(SendMessageW(window,WM_NCACTIVATE,FALSE,0)==TRUE && SendMessageW(window,WM_NCACTIVATE,TRUE,0)==TRUE,
        "activation changes do not request system non-client painting");
    verify_region("activation changes preserve the skin-shaped HWND region");

    // Drive the same client mouse/capture path used on the desktop. Restore the
    // original cursor even on a failed assertion; no mouse clicks go to another
    // application's HWND. The chosen corner points lie on opaque adjacent edges.
    struct CursorRestore{POINT point{};bool valid{GetCursorPos(&point)!=FALSE};
        ~CursorRestore(){if(valid)SetCursorPos(point.x,point.y);}} saved_cursor;
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
    const LONG x=monitor.rcWork.left+64,y=monitor.rcWork.top+64;
    struct ResizeCase{POINT start,delta;int edges;const char* label;};
    constexpr int left=1,right=2,top=4,bottom=8;
    const ResizeCase cases[]{
        {{2,180},{-24,0},left,"client capture resizes left edge without a system frame"},
        {{477,180},{24,0},right,"client capture resizes right edge without a system frame"},
        {{240,2},{0,-20},top,"client capture resizes top edge without a system frame"},
        {{240,357},{0,20},bottom,"client capture resizes bottom edge without a system frame"},
        {{2,12},{-24,-20},left|top,"opaque top-left grip resizes both edges"},
        {{477,12},{24,-20},right|top,"opaque top-right grip resizes both edges"},
        {{2,347},{-24,20},left|bottom,"opaque bottom-left grip resizes both edges"},
        {{477,347},{24,20},right|bottom,"opaque bottom-right grip resizes both edges"}};
    auto begin=[&](POINT point){
        POINT screen=point;ClientToScreen(window,&screen);
        if(!SetCursorPos(screen.x,screen.y))return false;
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
        return GetCapture()==window;
    };
    auto move=[&](POINT point,POINT delta){
        POINT screen{point.x+delta.x,point.y+delta.y};ClientToScreen(window,&screen);
        if(!SetCursorPos(screen.x,screen.y))return false;
        SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(point.x+delta.x,point.y+delta.y));return true;
    };
    for(const auto& item:cases){
        SetWindowPos(window,nullptr,x,y,480,360,SWP_NOZORDER|SWP_NOACTIVATE);
        RECT expected{};GetWindowRect(window,&expected);
        const bool captured=begin(item.start),moved=move(item.start,item.delta);
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(item.start.x+item.delta.x,item.start.y+item.delta.y));
        if(item.edges&left)expected.left+=item.delta.x;if(item.edges&right)expected.right+=item.delta.x;
        if(item.edges&top)expected.top+=item.delta.y;if(item.edges&bottom)expected.bottom+=item.delta.y;
        RECT actual{};GetWindowRect(window,&actual);
        if(!EqualRect(&expected,&actual))std::printf("resize geometry expected %ld,%ld,%ld,%ld actual %ld,%ld,%ld,%ld capture=%d move=%d\n",
            expected.left,expected.top,expected.right,expected.bottom,actual.left,actual.top,actual.right,actual.bottom,captured,moved);
        Check(captured && moved && EqualRect(&expected,&actual) && GetCapture()!=window && EqualRect(&actual,&host.last.bounds),item.label);
    }
    SetWindowPos(window,nullptr,x,y,480,360,SWP_NOZORDER|SWP_NOACTIVATE);RECT before{};GetWindowRect(window,&before);
    const bool captured=begin({477,347}),moved=move({477,347},{24,20});RECT grown{};GetWindowRect(window,&grown);
    MSG escape{};escape.hwnd=window;escape.message=WM_KEYDOWN;escape.wParam=VK_ESCAPE;
    const BOOL handled=api->translate_message(instance,&escape);RECT canceled{};GetWindowRect(window,&canceled);
    Check(captured && moved && !EqualRect(&before,&grown) && handled && EqualRect(&before,&canceled) &&
        GetCapture()!=window && IsWindowVisible(window),"Escape cancels a captured resize without closing the music window");
    const bool moving=begin({160,20}),translated=move({160,20},{20,12});
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(180,32));RECT relocated{};GetWindowRect(window,&relocated);
    OffsetRect(&before,20,12);
    Check(moving && translated && EqualRect(&before,&relocated) && GetCapture()!=window,
        "blank skin header moves the popup through client capture");
    verify_region("dragging and resize cancellation retain the correct skin region");
}
}

int wmain(int argc,wchar_t** argv) {
    if(argc!=2){std::fprintf(stderr,"Pass the absolute ttpbrowser.dll path\n");return 2;}
    HRESULT ole=OleInitialize(nullptr);if(FAILED(ole))return 2;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    HMODULE common=LoadLibraryW(L"comctl32.dll");
    auto version=common?reinterpret_cast<HRESULT(CALLBACK*)(DLLVERSIONINFO*)>(GetProcAddress(common,"DllGetVersion")):nullptr;
    DLLVERSIONINFO info{sizeof(info)};
    Check(version && SUCCEEDED(version(&info)) && info.dwMajorVersion>=6,"common-controls v6 matches production activation context");
    if(common)FreeLibrary(common);
    HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    Check(dll!=nullptr,"load optional DLL without linking import library");if(!dll)return 1;
    auto get=reinterpret_cast<ttp_browser_get_api_fn>(GetProcAddress(dll,"ttp_browser_get_api"));
    Check(get!=nullptr,"undecorated versioned export");if(!get)return 1;
    Check(get(0)==nullptr && get(999)==nullptr,"reject incompatible ABI versions");
    const auto* api=get(TTP_BROWSER_ABI_VERSION);
    Check(api && api->size>=sizeof(ttp_browser_api),"complete ABI table");if(!api)return 1;
    void* instance=reinterpret_cast<void*>(1);
    Check(FAILED(api->create(nullptr,nullptr,&instance)) && !instance,"reject malformed host and clear output");
    Host h;HWND owner=CreateWindowW(L"STATIC",L"TTPBrowser contract host",WS_OVERLAPPED,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    wchar_t temp[32768]{};GetTempPathW(32768,temp);
    const auto root=std::filesystem::path(temp)/(L"ttpbrowser-contract-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount()));
    const auto directory=root/L"songs",state=root/L"cachelist.xml";
    std::filesystem::create_directories(directory);
    const std::wstring runtime=std::filesystem::path(argv[1]).parent_path().wstring();
    const auto directory_text=directory.wstring(),state_text=state.wstring();
    ttp_browser_host host{sizeof(host),TTP_BROWSER_ABI_VERSION,&h,owner,nullptr,Submit,Query,Open,Changed,Setup};
    ttp_browser_config config{};config.size=sizeof(config);config.runtime_directory=runtime.c_str();
    config.state_file=state_text.c_str();config.download_directory=directory_text.c_str();
    // No test ever navigates the historical site.
    config.home_url=L"http://127.0.0.1:9/music-window-fixture";
    config.maximum_downloads=2;config.opacity=255;
    config.initial_state={sizeof(ttp_browser_state),{30,30,670,510},{},0};
    // Place the old ABI prefix against an inaccessible page. A full-struct
    // copy, or a read of an incomplete callback pointer, must fail this probe.
    SYSTEM_INFO system{};GetSystemInfo(&system);
    auto* allocation=static_cast<unsigned char*>(VirtualAlloc(nullptr,system.dwPageSize*2,
        MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(allocation!=nullptr,"allocate guarded legacy host fixture");
    if(allocation){
        DWORD previous{};
        const bool guarded=VirtualProtect(allocation+system.dwPageSize,system.dwPageSize,PAGE_NOACCESS,&previous)!=FALSE;
        Check(guarded,"protect page following legacy host allocation");
        if(guarded)for(const size_t size:{TTP_BROWSER_HOST_V1_SIZE,TTP_BROWSER_HOST_V1_SIZE+1}){
            auto* legacy=reinterpret_cast<ttp_browser_host*>(allocation+system.dwPageSize-size);
            std::memcpy(legacy,&host,TTP_BROWSER_HOST_V1_SIZE);legacy->size=static_cast<uint32_t>(size);
            void* old_instance{};
            const HRESULT result=api->create(legacy,&config,&old_instance);
            Check(SUCCEEDED(result) && old_instance,"old or partial host extension is accepted without an out-of-bounds read");
            if(old_instance)api->destroy(old_instance);
        }
        VirtualFree(allocation,0,MEM_RELEASE);
    }
    Check(SUCCEEDED(api->create(&host,&config,&instance)) && instance,"create independent instance");
    if(!instance)return 1;
    IDispatch* external{};Check(SUCCEEDED(api->external_dispatch(instance,&external)) && external,"external IDispatch available before navigation");
    IDispatch* inner{};
    if(external) {
        for(const auto* alias:{L"ttplayer",L"ttplayer2",L"ttplayer3",L"ttplayer4",L"ttplayer5",L"ttplayer6"}) {
            VARIANT value{};const auto hr=Invoke(external,alias,{},&value,DISPATCH_PROPERTYGET);
            Check(SUCCEEDED(hr) && value.vt==VT_DISPATCH && value.pdispVal,"legacy property aliases return business dispatch");
            if(!inner && value.vt==VT_DISPATCH && value.pdispVal){inner=value.pdispVal;inner->AddRef();}
            VariantClear(&value);
        }
    }
    if(inner) {
        VARIANT value{};Check(SUCCEEDED(Invoke(inner,L"GetPlayerInfo",{Number(1)},&value)) && value.vt==VT_BSTR &&
            value.bstrVal && std::wstring(value.bstrVal)==L"5.7.9","legacy player version query");VariantClear(&value);
        Check(Invoke(inner,L"GetCacheInfo",{Text(L"a"),Text(L"b"),Text(L"c")},&value)==E_FAIL,"legacy cache stub preserves Invoke failure");VariantClear(&value);
        Check(FAILED(Invoke(inner,L"AddSoundToPlay",{Text(L"http://fixture/a.mp3")})),"reject missing COM arguments");
        Check(FAILED(Invoke(inner,L"AddDownloadInfoByTid",{Text(L"http://fixture/a.mp3"),Text(L"a"),Text(L"t"),Text(L"id")})),"require all five ByTid arguments");
        Check(SUCCEEDED(Invoke(inner,L"BeginAddSongs")),"begin batch");
        Check(SUCCEEDED(Invoke(inner,L"AddSong",{Text(L"http://fixture/first.mp3"),Text(L"artist one"),Text(L"title one")})),"stage first song");
        Check(SUCCEEDED(Invoke(inner,L"AddSong",{Text(L"http://fixture/second.mp3"),Text(L"artist two"),Text(L"title two")})),"stage second song");
        Pump();Check(h.submits==0,"staging never mutates player");
        Check(SUCCEEDED(Invoke(inner,L"EndAddSongs",{Number(0),Number(1),Number(1)})),"end batch accepted");Pump();
        Check(h.submits==1 && h.submitted.size()==2 && h.submitted[0].artist==L"artist one" &&
            h.submitted[1].title==L"title two" && h.mode[0]==0 && h.mode[1]==1 && h.mode[2]==1,"one ordered commit preserves metadata and modes");
        Check(SUCCEEDED(Invoke(inner,L"AddSoundToPlayByTid",{Text(L"http://fixture/tid.mp3"),Text(L"artist"),Text(L"title"),Text(L"tid-string"),Number(1)})),"ByTid accepts source and string identifier");Pump();
        Check(h.submits==2 && h.submitted.size()==3 && h.submitted.back().tid==L"tid-string","Tid metadata reaches host");
    }
    uint64_t id{};ttp_browser_track song{L"http://fixture/download.mp3",L"artist",L"download",L"tid"};
    Check(SUCCEEDED(api->enqueue(instance,&song,&id)) && id,"download reaches DLL-owned queue");
    const auto saved=directory/L"download.mp3";
    for(unsigned i=0;i<150 && !std::filesystem::exists(saved);++i)Pump(20);
    std::ifstream in(saved,std::ios::binary);std::vector<unsigned char> actual{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
    Check(actual==h.bytes && h.opens>0,"downloaded file matches host transport bytes");
    ttp_browser_track bad{L"file:///C:/invalid",L"",L"",L""};id=9;
    Check(FAILED(api->enqueue(instance,&bad,&id)) && !id,"download rejects non-HTTP source");
    Check(SUCCEEDED(api->show(instance,TTP_BROWSER_VIEW_DOWNLOADS)),"embedded download view opens without navigating music homepage");
    const HWND browser=api->window(instance);
    HWND tooltip=FindWindowExW(nullptr,nullptr,TOOLTIPS_CLASSW,nullptr);
    while(tooltip && GetWindow(tooltip,GW_OWNER)!=browser)tooltip=FindWindowExW(nullptr,tooltip,TOOLTIPS_CLASSW,nullptr);
    Check(tooltip && SendMessageW(tooltip,TTM_GETTOOLCOUNT,0,0)==TTP_BROWSER_CONTROL_COUNT,"all browser buttons retain tooltip registration");
    wchar_t before_text[128]{};TOOLINFOW before_tool{sizeof(before_tool)};
    before_tool.hwnd=browser;before_tool.uId=TTP_BROWSER_CLOSE+1;before_tool.lpszText=before_text;
    Check(tooltip && SendMessageW(tooltip,TTM_GETTOOLINFOW,0,reinterpret_cast<LPARAM>(&before_tool)) &&
        std::wstring(before_text)==L"关闭","tooltip text is readable into a writable buffer");
    RECT initial{};GetWindowRect(browser,&initial);
    for(int n=0;n<4;++n){
        SetWindowPos(browser,nullptr,0,0,initial.right-initial.left+40+n*20,initial.bottom-initial.top+30+n*10,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        Check(SUCCEEDED(api->configure(instance,&config)),"repeat layout/configuration under common-controls v6");
    }
    wchar_t after_text[128]{};TOOLINFOW after_tool{sizeof(after_tool)};
    after_tool.hwnd=browser;after_tool.uId=TTP_BROWSER_CLOSE+1;after_tool.lpszText=after_text;
    Check(tooltip && SendMessageW(tooltip,TTM_GETTOOLINFOW,0,reinterpret_cast<LPARAM>(&after_tool)) &&
        std::wstring(after_text)==L"关闭" && after_tool.rect.left>before_tool.rect.left &&
        SendMessageW(tooltip,TTM_GETTOOLCOUNT,0,0)==TTP_BROWSER_CONTROL_COUNT,"resize updates tooltip bounds without changing text or duplicating tools");
    CheckBackgroundPatches(api,instance);
    CheckBorderlessWindow(api,instance,h);
    ttp_browser_state ws{sizeof(ws)};Check(SUCCEEDED(api->get_state(instance,&ws)) &&
        (ws.flags&TTP_BROWSER_STATE_VISIBLE) && (ws.flags&TTP_BROWSER_STATE_DOWNLOADS),"view state reflects download tab");
    Check(SUCCEEDED(api->show(instance,TTP_BROWSER_VIEW_HIDE)),"hide window preserves module and queue");
    api->destroy(instance);instance=nullptr;Pump();
    Check(std::filesystem::exists(state) && std::filesystem::file_size(state)>0,"queue persisted before module destruction");
    if(inner){Check(FAILED(Invoke(inner,L"BeginAddSongs")),"retained dispatch safely rejects calls after destruction");inner->Release();}
    if(external)external->Release();
    Check(SUCCEEDED(api->create(&host,&config,&instance)) && instance,"queue state can be reopened after shutdown");
    if(instance)api->destroy(instance);
    DestroyWindow(owner);FreeLibrary(dll);OleUninitialize();
    std::wprintf(L"Artifacts: %ls\n",root.c_str());
    std::printf("%d failure(s)\n",failures);return failures?1:0;
}
