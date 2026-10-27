#include "ttpbrowser.h"
#include <commctrl.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// Black-box tests of the exported DLL, not a second copy of its implementation.
// Every message is addressed to our own hidden HWNDs. The host returns only
// in-memory streams; no real pointer movement, browser navigation, Shell action,
// user player, or network endpoint is involved.
namespace {
int failures{},checks{};
void Check(bool condition,const char* label) {
    ++checks;if(!condition)++failures;
    std::printf("%s %s\n",condition?"PASS":"FAIL",label);std::fflush(stdout);
}
void Require(bool condition,const char* label) {
    Check(condition,label);if(!condition)throw std::runtime_error(label);
}
void Pump() {
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
        TranslateMessage(&message);DispatchMessageW(&message);
    }
}
template<class Predicate> bool Until(Predicate ready,DWORD milliseconds=2500) {
    const auto start=GetTickCount();
    do {
        Pump();if(ready())return true;
        MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
    } while(GetTickCount()-start<milliseconds);
    Pump();return ready();
}
struct Track {std::wstring source,artist,title,tid;};
Track CopyTrack(const ttp_browser_track& track) {
    return {track.source?track.source:L"",track.artist?track.artist:L"",
        track.title?track.title:L"",track.tid?track.tid:L""};
}
struct Host {
    std::atomic_uint failed_opens{},blocked_opens{},cancellations{},finite_opens{};
    unsigned submits{},lyric_queries{};
    int mode[3]{};
    std::vector<Track> submitted;
};
HRESULT WINAPI Submit(void* context,const ttp_browser_track* tracks,uint32_t count,int a,int b,int c) {
    auto& host=*static_cast<Host*>(context);++host.submits;
    host.submitted.clear();host.mode[0]=a;host.mode[1]=b;host.mode[2]=c;
    for(uint32_t i=0;i<count;++i)host.submitted.push_back(CopyTrack(tracks[i]));return S_OK;
}
HRESULT WINAPI Lyric(void* context,const wchar_t*,BSTR* result) {
    ++static_cast<Host*>(context)->lyric_queries;if(!result)return E_POINTER;*result=nullptr;return S_FALSE;
}
HRESULT WINAPI Open(void* context,const wchar_t* url,HANDLE canceled,IStream** result,BOOL* persist) {
    if(!url || !result || !persist)return E_INVALIDARG;*result=nullptr;*persist=TRUE;
    auto& host=*static_cast<Host*>(context);const std::wstring source(url);
    if(source.find(L"failed")!=source.npos){++host.failed_opens;return E_FAIL;}
    if(source.find(L"blocked")!=source.npos) {
        ++host.blocked_opens;
        if(WaitForSingleObject(canceled,3000)==WAIT_OBJECT_0) {
            ++host.cancellations;return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    ++host.finite_opens;IStream* stream{};
    HRESULT hr=CreateStreamOnHGlobal(nullptr,TRUE,&stream);if(FAILED(hr))return hr;
    const char bytes[]="mouse contract fixture; deterministic content";ULONG count{};
    hr=stream->Write(bytes,sizeof(bytes),&count);LARGE_INTEGER zero{};
    if(SUCCEEDED(hr))hr=stream->Seek(zero,STREAM_SEEK_SET,nullptr);
    if(FAILED(hr)){stream->Release();return hr;}*result=stream;return S_OK;
}
VARIANT Text(const wchar_t* text) {VARIANT value{};value.vt=VT_BSTR;value.bstrVal=SysAllocString(text);return value;}
HRESULT Invoke(IDispatch* dispatch,const wchar_t* name,std::vector<VARIANT> arguments={},
    VARIANT* result=nullptr,WORD flags=DISPATCH_METHOD) {
    LPOLESTR member=const_cast<LPOLESTR>(name);DISPID id{};
    HRESULT hr=dispatch->GetIDsOfNames(IID_NULL,&member,1,LOCALE_USER_DEFAULT,&id);
    std::reverse(arguments.begin(),arguments.end());DISPPARAMS params{};
    params.rgvarg=arguments.data();params.cArgs=static_cast<UINT>(arguments.size());
    if(SUCCEEDED(hr))hr=dispatch->Invoke(id,IID_NULL,LOCALE_USER_DEFAULT,flags,&params,result,nullptr,nullptr);
    for(auto& argument:arguments)VariantClear(&argument);return hr;
}
void Click(HWND window,int x,int y) {
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(x,y));Pump();
}
void DoubleClick(HWND window,int x,int y) {
    Click(window,x,y);SendMessageW(window,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(x,y));
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(x,y));Pump();
}
struct MenuObserver {unsigned opens{};std::vector<std::wstring> labels;std::vector<UINT> states;};
LRESULT CALLBACK Observe(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
    auto& observer=*reinterpret_cast<MenuObserver*>(data);
    if(message==WM_INITMENUPOPUP) {
        ++observer.opens;
        observer.labels.clear();observer.states.clear();const auto menu=reinterpret_cast<HMENU>(wp);
        for(int index=0;index<GetMenuItemCount(menu);++index){wchar_t label[128]{};
            GetMenuStringW(menu,index,label,128,MF_BYPOSITION);observer.labels.emplace_back(label);
            observer.states.push_back(GetMenuState(menu,index,MF_BYPOSITION));}
        // A regression must report a failed assertion, not wait for human input.
        PostMessageW(hwnd,WM_CANCELMODE,0,0);
    }
    return DefSubclassProc(hwnd,message,wp,lp);
}
struct MenuObservation {
    HWND outer{},frame{};MenuObserver state;
    ~MenuObservation(){if(frame)RemoveWindowSubclass(frame,Observe,1);if(outer)RemoveWindowSubclass(outer,Observe,1);}
};
void RightClick(HWND target,int x,int y) {
    SendMessageW(target,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(x,y));
    SendMessageW(target,WM_RBUTTONUP,0,MAKELPARAM(x,y));Pump();
}
RECT RelativeBounds(HWND child,HWND parent) {
    RECT rect{};GetWindowRect(child,&rect);MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&rect),2);return rect;
}
struct Instance {
    const ttp_browser_api* api{};void* value{};IDispatch* external{};
    ~Instance(){if(external)external->Release();if(value)api->destroy(value);}
};
size_t StateCount(const std::filesystem::path& file,const char* value) {
    std::ifstream input(file,std::ios::binary);std::string text{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    size_t count{},offset{};
    while((offset=text.find(value,offset))!=text.npos){++count;offset+=std::strlen(value);}return count;
}
bool StateContains(const std::filesystem::path& file,const char* value){return StateCount(file,value)!=0;}
void CheckOldHosts(const ttp_browser_api* api,const ttp_browser_host& host,const ttp_browser_config& config) {
    SYSTEM_INFO info{};GetSystemInfo(&info);
    auto* pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,2*info.dwPageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Require(pages!=nullptr,"allocate guarded optional-callback ABI fixture");DWORD old{};
    const bool guarded=VirtualProtect(pages+info.dwPageSize,info.dwPageSize,PAGE_NOACCESS,&old)!=FALSE;
    Check(guarded,"protect memory immediately following advertised host size");
    if(guarded)for(const size_t size:{size_t(TTP_BROWSER_HOST_V1_SIZE),
            offsetof(ttp_browser_host,resolve_associated_lyric),offsetof(ttp_browser_host,resolve_associated_lyric)+1}) {
        auto* prefix=reinterpret_cast<ttp_browser_host*>(pages+info.dwPageSize-size);
        std::memcpy(prefix,&host,size);prefix->size=static_cast<uint32_t>(size);
        Instance instance{api};
        const auto result=api->create(prefix,&config,&instance.value);
        Check(SUCCEEDED(result) && instance.value,"old host and truncated optional callbacks do not read beyond advertised size");
        if(instance.value && SUCCEEDED(api->external_dispatch(instance.value,&instance.external))) {
            const auto added=Invoke(instance.external,L"AddRecomdSound",{Text(L"https://fixture.invalid/legacy.mp3"),Text(L"Artist"),Text(L"Legacy")});
            const auto cleared=Invoke(instance.external,L"ClearRecomdList");Pump();
            Check(added==S_FALSE && cleared==S_OK,"old hosts safely decline recommendation additions and accept the clear no-op");
        }else Check(false,"legacy host exposes bridge without browser navigation");
    }
    VirtualFree(pages,0,MEM_RELEASE);
}
void CheckRecommendationCompatibility(Instance& instance,Host& host,ttp_browser_config& config) {
    Check(Invoke(instance.external,L"AddRecomdSound",{Text(L"https://fixture.invalid/recommend-1.mp3"),Text(L"Artist one"),Text(L"Title one")})==S_FALSE,
        "legacy recommendation addition is declined without creating a recommendation list");
    Check(FAILED(Invoke(instance.external,L"AddRecomdSound",{Text(L"https://fixture.invalid/incomplete.mp3")})),
        "unsupported recommendation method still rejects malformed arguments");
    Pump();
    Check(host.submits==0,"unsupported recommendation calls never submit songs or start playback");
    Check(Invoke(instance.external,L"ClearRecomdList")==S_OK,"legacy recommendation clear is a compatible no-op");Pump();
    Check(host.submits==0,"recommendation clear does not change playback");
    config.accept_recommendations=FALSE;
    Require(SUCCEEDED(instance.api->configure(instance.value,&config)),"disable accepting recommendations");
    Check(Invoke(instance.external,L"AddRecomdSound",{Text(L"https://fixture.invalid/disabled.mp3"),Text(L"Artist"),Text(L"Disabled")})==S_FALSE,
        "disabled recommendation reception returns S_FALSE");Pump();
    Check(host.submits==0,"disabled recommendation reception does not submit songs");
    Check(SUCCEEDED(Invoke(instance.external,L"ClearRecomdList")),"clear remains accepted while recommendation reception is disabled");Pump();
    Check(host.submits==0,"clear remains free of playback side effects while reception is disabled");
    config.accept_recommendations=TRUE;
    Require(SUCCEEDED(instance.api->configure(instance.value,&config)),"restore accepting recommendations");
    Check(Invoke(instance.external,L"AddRecomdSound",{Text(L"https://fixture.invalid/right-click.mp3"),Text(L"Artist"),Text(L"Right-click trap")})==S_FALSE,
        "a page recommendation attempt remains inert before right-click probes");Pump();
    VARIANT inner{};
    if(SUCCEEDED(Invoke(instance.external,L"ttplayer",{},&inner,DISPATCH_PROPERTYGET)) && inner.vt==VT_DISPATCH && inner.pdispVal)
        Check(SUCCEEDED(Invoke(inner.pdispVal,L"AddBrowserHyper",{Text(L"Fixture link"),Text(L"https://fixture.invalid/never-open")})),
            "install a hyperlink without opening its destination before context-menu probes");
    else Check(false,"get legacy business dispatch for hyperlink fixture");
    VariantClear(&inner);Pump();
}
void CheckMouse(Instance& instance,Host& host,const std::filesystem::path& directory,const std::filesystem::path& state) {
    const auto* api=instance.api;const HWND outer=api->window(instance.value);
    MenuObservation observation{outer,nullptr};auto& observer=observation.state;
    Require(SetWindowSubclass(outer,Observe,1,reinterpret_cast<DWORD_PTR>(&observer))!=FALSE,
        "observe and automatically dismiss border popup menus");
    RightClick(outer,280,15);
    Check(observer.opens==1 && observer.labels==std::vector<std::wstring>{L"音乐窗",L"下载管理"} &&
        observer.states.size()==2 && (observer.states[0]&MF_CHECKED) && !(observer.states[1]&MF_CHECKED),
        "border menu checks Music before opening the download view");
    // The fallback skin's download tab is 188,5..268,29. A direct click creates
    // the downloads child while keeping the top-level window hidden.
    Click(outer,210,15);
    const HWND frame=FindWindowExW(outer,nullptr,L"TTPBrowser.Downloads.v1",nullptr);
    const HWND list=FindWindowExW(frame,nullptr,L"TTPBrowser.DownloadList.v1",nullptr);
    Require(frame && list,"create hidden download frame and custom list through normal tab activation");
    observation.frame=frame;
    Require(SetWindowSubclass(frame,Observe,1,reinterpret_cast<DWORD_PTR>(&observer))!=FALSE,
        "observe and automatically dismiss unexpected child popup menus");
    auto refresh=[&]{SendMessageW(frame,WM_SHOWWINDOW,TRUE,0);};
    auto enqueue=[&](const wchar_t* source,const wchar_t* title){
        ttp_browser_track track{source,L"Fixture artist",title,L"fixture-tid"};uint64_t id{};
        Require(SUCCEEDED(api->enqueue(instance.value,&track,&id)) && id,"enqueue memory-only fixture task");
    };
    // Bundled download skin geometry: banner 36, header 25, rows 24.
    constexpr int first=35,second=59,blank=190;
    enqueue(L"https://fixture.invalid/failed.mp3",L"Failed task");
    Require(Until([&]{return host.failed_opens==1 && StateContains(state,"state=\"failed\"");}),"failed fixture settles in the saved queue");refresh();
    const auto before=host.submits;DoubleClick(list,80,first);
    Check(Until([&]{return host.failed_opens==2;}) && host.submits==before,"double-clicking a failed download retries it without playback");
    Require(Until([&]{return StateContains(state,"state=\"failed\"");}),"retried fixture returns to a failed state");
    enqueue(L"https://fixture.invalid/blocked.mp3",L"Running task");
    Require(Until([&]{return host.blocked_opens==1;}),"controlled download callback is running");refresh();
    DoubleClick(list,80,second);
    Check(Until([&]{return host.cancellations==1 && StateContains(state,"state=\"paused\"");}) && host.submits==before,
        "double-clicking a running download signals cancellation and saves paused state without playback");refresh();
    DoubleClick(list,80,second);
    Check(Until([&]{return host.blocked_opens==2;}) && host.submits==before,"double-clicking a paused download restarts it without playback");refresh();
    DoubleClick(list,80,second);
    Require(Until([&]{return host.cancellations==2 && StateContains(state,"state=\"paused\"");}),"pause resumed fixture before completing other tasks");
    Click(list,80,first);DoubleClick(list,80,blank);
    Check(host.submits==before,"double-clicking empty active-list space does not play the previous selection");
    enqueue(L"https://fixture.invalid/complete-one.mp3",L"Complete one");
    enqueue(L"https://fixture.invalid/complete-two.mp3",L"Complete two");
    Require(Until([&]{return StateCount(state,"state=\"complete\"")==2 &&
        std::filesystem::exists(directory/L"Complete one.mp3") && std::filesystem::exists(directory/L"Complete two.mp3");}),
        "both finite memory streams publish complete files");refresh();Click(frame,20,97);
    host.submits=0;host.submitted.clear();Click(list,5,first);
    Check(host.submits==1 && host.submitted.size()==1 && host.submitted[0].source==(directory/L"Complete one.mp3").wstring() &&
        host.mode[0]==0 && host.mode[1]==1 && host.mode[2]==1,"single-clicking completed first-column play glyph submits the local file once");
    const auto icon_submits=host.submits;SendMessageW(list,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(5,first));
    SendMessageW(list,WM_LBUTTONUP,0,MAKELPARAM(5,first));Pump();
    Check(host.submits==icon_submits,"play-glyph double-click message does not replay its first click");
    DoubleClick(list,80,second);
    Check(host.submits==icon_submits+1 && host.submitted.size()==1 && host.submitted[0].source==(directory/L"Complete two.mp3").wstring(),
        "double-clicking a completed row plays that row's local file");
    const auto completed_submits=host.submits;DoubleClick(list,80,blank);
    Check(host.submits==completed_submits,"double-clicking empty completed-list space never replays the previous selection");
    Click(list,80,second);Click(frame,20,97);SendMessageW(list,WM_KEYDOWN,VK_RETURN,0);Pump();
    Check(host.submits==completed_submits+1 && host.submitted.size()==1 && host.submitted[0].source==(directory/L"Complete two.mp3").wstring(),
        "reselecting the current category retains the selected completed row");
    const auto initial=RelativeBounds(list,frame);RECT body{};GetClientRect(frame,&body);
    const int middle=(initial.top+body.bottom)/2;
    Click(frame,initial.left-3,middle);const auto collapsed=RelativeBounds(list,frame);
    Check(collapsed.left==5 && collapsed.top==initial.top,"clicking splitter center collapses the category pane");
    Click(frame,2,middle);const auto restored=RelativeBounds(list,frame);
    Check(EqualRect(&initial,&restored)!=FALSE,"clicking collapsed splitter center restores the prior category width");
    // The explicitly requested border menu only switches views; child blank
    // areas must not inherit it, even after legacy recommendation calls.
    const auto before_context=host.submits;
    const auto right=[&](HWND target,int x,int y,const char* label){
        const auto menus=observer.opens;RightClick(target,x,y);Check(observer.opens==menus && host.submits==before_context,label);
    };
    const std::vector<std::wstring> view_labels{L"音乐窗",L"下载管理"};
    const auto border_menu=[&](bool downloads,const char* label){
        const auto menus=observer.opens;RightClick(outer,280,15);
        Check(observer.opens==menus+1 && observer.labels==view_labels && observer.states.size()==2 &&
            !!(observer.states[0]&MF_CHECKED)==!downloads && !!(observer.states[1]&MF_CHECKED)==downloads &&
            host.submits==before_context,label);
    };
    border_menu(true,"border menu contains only the two view commands and checks Downloads");
    right(outer,210,15,"right-clicking the download tab does not open the border menu");
    right(outer,200,180,"content coordinates cannot invoke the border menu through the outer window");
    right(frame,20,190,"category blank right-click does not open recommendations");
    right(frame,250,15,"download banner right-click does not open recommendations");
    right(list,80,blank,"empty download-list right-click does not open a menu");
    right(list,80,10,"download-header right-click does not open a menu");
    if(const HWND help=GetDlgItem(frame,202))right(help,5,5,"download help-button right-click does not open recommendations");
    else Check(false,"download help button exists");
    const auto keyboard_menus=observer.opens;
    SendMessageW(outer,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(outer),MAKELPARAM(-1,-1));Pump();
    Check(observer.opens==keyboard_menus+1 && observer.labels==view_labels && host.submits==before_context,
        "keyboard context menu on the outer window exposes only view switching");
    const auto child_menus=observer.opens;
    SendMessageW(outer,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(frame),MAKELPARAM(-1,-1));Pump();
    Check(observer.opens==child_menus,"propagated child context menu cannot open the border menu");
    ttp_browser_state final{sizeof(final)};
    Check(SUCCEEDED(api->get_state(instance.value,&final)) && !(final.flags&(TTP_BROWSER_STATE_VISIBLE|TTP_BROWSER_STATE_WEBVIEW2|TTP_BROWSER_STATE_IE)) &&
        !IsWindowVisible(outer) && host.lyric_queries==0,"all probes keep the fixture hidden without browser startup or destructive file actions");
}
} // namespace

int wmain(int argc,wchar_t** argv) {
    if(argc!=2){std::fprintf(stderr,"Pass the absolute ttpbrowser.dll path\n");return 2;}
    const auto ole=OleInitialize(nullptr);if(FAILED(ole))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_WIN95_CLASSES};InitCommonControlsEx(&common);
    const auto dll=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!dll){std::fprintf(stderr,"Cannot load DLL: %lu\n",GetLastError());OleUninitialize();return 2;}
    try {
        const auto get=reinterpret_cast<ttp_browser_get_api_fn>(GetProcAddress(dll,"ttp_browser_get_api"));
        Require(get!=nullptr,"load real DLL through its exported API");const auto* api=get(TTP_BROWSER_ABI_VERSION);
        Require(api && api->size>=sizeof(ttp_browser_api),"DLL exposes the complete supported ABI");
        wchar_t temp[MAX_PATH]{};Require(GetTempPathW(MAX_PATH,temp)!=0,"resolve temporary artifact root");
        const auto root=std::filesystem::path(temp)/(L"ttpbrowser-mouse-contract-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount()));
        const auto directory=root/L"songs",state=root/L"queue.xml";std::filesystem::create_directories(directory);
        const auto runtime=std::filesystem::absolute(argv[1]).parent_path().wstring(),folder=directory.wstring(),file=state.wstring();
        Host host_state;ttp_browser_host host{};host.size=sizeof(host);host.version=TTP_BROWSER_ABI_VERSION;
        host.context=&host_state;host.submit_tracks=Submit;host.open_stream=Open;
        host.resolve_associated_lyric=Lyric;
        ttp_browser_config config{};config.size=sizeof(config);config.runtime_directory=runtime.c_str();config.download_directory=folder.c_str();
        config.state_file=file.c_str();config.home_url=L"https://fixture.invalid/never-navigate";config.maximum_downloads=1;config.opacity=255;
        config.accept_recommendations=TRUE;config.initial_state={sizeof(ttp_browser_state),{30,30,510,516},{},0};
        CheckOldHosts(api,host,config);
        Require(host_state.submits==0 && host_state.lyric_queries==0,"old-host compatibility calls have no playback or file-action side effects");
        {
            Instance instance{api};Require(SUCCEEDED(api->create(&host,&config,&instance.value)) && instance.value,"create isolated mouse-contract instance");
            Require(SUCCEEDED(api->external_dispatch(instance.value,&instance.external)) && instance.external,"get bridge before loading any webpage");
            CheckRecommendationCompatibility(instance,host_state,config);CheckMouse(instance,host_state,directory,state);
        }
        std::wprintf(L"Artifacts: %ls\n",root.c_str());
    }catch(const std::exception& error){++failures;std::fprintf(stderr,"Stopped: %s\n",error.what());}
    FreeLibrary(dll);OleUninitialize();
    std::printf("%d checks, %d failure(s)\n",checks,failures);return failures?1:0;
}
