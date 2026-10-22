#include "ttpbrowser.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
struct Song {std::wstring source,artist,title,tid;};
struct Host {std::vector<Song> songs;unsigned commits{},queries{};};
int failures{};
void Check(bool ok,const char* label){std::printf("%s %s\n",ok?"PASS":"FAIL",label);std::fflush(stdout);if(!ok)++failures;}
void Pump(DWORD ms){DWORD start=GetTickCount();do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}while(GetTickCount()-start<ms);}
HRESULT WINAPI Submit(void* p,const ttp_browser_track* tracks,uint32_t n,int,int,int){auto& h=*static_cast<Host*>(p);++h.commits;
    for(uint32_t i=0;i<n;++i)h.songs.push_back({tracks[i].source,tracks[i].artist,tracks[i].title,tracks[i].tid});return S_OK;}
HRESULT WINAPI Query(void* p,uint32_t kind,uint32_t selector,BSTR* value){++static_cast<Host*>(p)->queries;*value=SysAllocString(kind==1 && selector==1?L"5.7.9":L"0");return *value?S_OK:E_OUTOFMEMORY;}
HRESULT WINAPI Open(void*,const wchar_t*,HANDLE,IStream** out,BOOL* persist){*persist=TRUE;auto hr=CreateStreamOnHGlobal(nullptr,TRUE,out);
    if(FAILED(hr))return hr;const char bytes[]="browser-script-download";ULONG n{};hr=(*out)->Write(bytes,sizeof(bytes)-1,&n);LARGE_INTEGER zero{};(*out)->Seek(zero,STREAM_SEEK_SET,nullptr);return hr;}
void WINAPI Changed(void*,const ttp_browser_state*){}
HRESULT WINAPI Setup(void*,const wchar_t*,const wchar_t*,const wchar_t*){return E_NOTIMPL;}
void Capture(HWND window,const std::filesystem::path& path){RECT r{};GetWindowRect(window,&r);const int w=r.right-r.left,h=r.bottom-r.top;
    HDC dc=GetDC(nullptr),memory=CreateCompatibleDC(dc);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;
    info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels{};HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto old=SelectObject(memory,bitmap);
    BitBlt(memory,0,0,w,h,dc,r.left,r.top,SRCCOPY|CAPTUREBLT);BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+w*h*4;
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(&file),sizeof(file));out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));out.write(static_cast<const char*>(pixels),w*h*4);
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,dc);
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=5){std::fprintf(stderr,"runtime_tests DLL URL engine artifact_directory\n");return 2;}
    OleInitialize(nullptr);HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!dll)return 2;
    auto get=reinterpret_cast<ttp_browser_get_api_fn>(GetProcAddress(dll,"ttp_browser_get_api"));if(!get)return 2;
    auto api=get(TTP_BROWSER_ABI_VERSION);Host h;HWND owner=CreateWindowW(L"STATIC",L"Runtime test owner",WS_OVERLAPPED,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    std::filesystem::path root(argv[4]);std::filesystem::create_directories(root/L"songs");
    const auto runtime=std::filesystem::path(argv[1]).parent_path().wstring(),state=(root/L"cachelist.xml").wstring(),songs=(root/L"songs").wstring();
    ttp_browser_host host{sizeof(host),TTP_BROWSER_ABI_VERSION,&h,owner,nullptr,Submit,Query,Open,Changed,Setup};
    ttp_browser_config config{};config.size=sizeof(config);config.runtime_directory=runtime.c_str();config.state_file=state.c_str();config.download_directory=songs.c_str();
    config.home_url=argv[2];config.engine=static_cast<uint32_t>(_wtoi(argv[3]));config.opacity=255;config.maximum_downloads=2;
    config.initial_state={sizeof(ttp_browser_state),{60,60,820,660},{},0};void* instance{};
    Check(SUCCEEDED(api->create(&host,&config,&instance)) && instance,"runtime instance created");if(!instance)return 1;
    Check(SUCCEEDED(api->show(instance,TTP_BROWSER_VIEW_MUSIC)),"show music browser");
    for(unsigned i=0;i<150 && h.commits<2;++i)Pump(100);
    Pump(500);
    ttp_browser_state actual{sizeof(actual)};api->get_state(instance,&actual);
    Check(GetModuleHandleW(L"WebView2Loader.dll")==nullptr,"browser works without a loaded external WebView2Loader DLL");
    Check((actual.flags&(config.engine==2?TTP_BROWSER_STATE_WEBVIEW2:TTP_BROWSER_STATE_IE))!=0,"requested engine is active");
    Check(h.queries>=6,"all six window.external aliases call native query");
    Check(h.commits==2 && h.songs.size()==3,"real page submits ordered batch and ByTid commit");
    Check(h.songs.size()==3 && h.songs[0].artist==L"first artist" && h.songs[1].title==L"second title" && h.songs[2].tid==L"web-tid","real JavaScript preserves argument order and Tid");
    const auto downloaded=root/L"songs"/L"web-download.mp3";
    for(unsigned i=0;i<50 && !std::filesystem::exists(downloaded);++i)Pump(100);
    std::ifstream in(downloaded,std::ios::binary);std::string bytes{std::istreambuf_iterator<char>(in),{}};
    Check(bytes=="browser-script-download","page download command uses DLL queue and host stream");
    Capture(api->window(instance),root/L"browser.bmp");
    // Navigating away must discard a batch staged by the previous document.
    std::wstring next=argv[2];next+=L"?phase=second";config.home_url=next.c_str();
    Check(SUCCEEDED(api->configure(instance,&config)),"navigate to next trusted document");
    Pump(1500);Check(h.commits==2 && h.songs.size()==3,"uncommitted old-document batch is revoked on navigation");
    // A different host spelling is a distinct origin; its page must not obtain native access.
    std::wstring foreign=argv[2];auto at=foreign.find(L"127.0.0.1");if(at!=std::wstring::npos)foreign.replace(at,9,L"localhost");
    // Use page-owned navigation so home/trust configuration remains unchanged.
    next=argv[2];next+=L"?phase=redirect";config.home_url=next.c_str();api->configure(instance,&config);Pump(1800);
    Check(h.commits==2 && h.songs.size()==3,"foreign-origin navigation cannot invoke native player");
    api->destroy(instance);Pump(500);DestroyWindow(owner);FreeLibrary(dll);OleUninitialize();
    std::printf("%d failure(s)\n",failures);return failures?1:0;
}
