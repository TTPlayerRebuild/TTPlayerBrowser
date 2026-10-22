#include "downloads.h"
#include "download_skin.h"
#include "ttplayer/audio/http_download_queue.h"
#include "ttplayer/core/text.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <wrl/client.h>

using ttplayer::audio::HttpDownloadQueue;
using ttplayer::audio::DownloadState;
using namespace std::chrono_literals;
static void Require(bool result,const char* message){if(!result)throw std::runtime_error(message);}
template<class F> static void Wait(F condition,const char* message){
    const auto end=std::chrono::steady_clock::now()+5s;
    while(!condition()) {if(std::chrono::steady_clock::now()>end)throw std::runtime_error(message);std::this_thread::sleep_for(5ms);}
}
static HRESULT MemoryStream(IStream** out) {
    *out=nullptr;Microsoft::WRL::ComPtr<IStream> stream;
    auto hr=CreateStreamOnHGlobal(nullptr,TRUE,&stream);if(FAILED(hr))return hr;
    const char bytes[]="TTPLAYER DOWNLOAD TEST: deterministic finite content";
    ULONG written{};hr=stream->Write(bytes,sizeof(bytes),&written);if(FAILED(hr))return hr;
    LARGE_INTEGER zero{};hr=stream->Seek(zero,STREAM_SEEK_SET,nullptr);if(FAILED(hr))return hr;
    *out=stream.Detach();return S_OK;
}
static std::filesystem::path TestRoot(){
    wchar_t temp[MAX_PATH]{};Require(GetTempPathW(MAX_PATH,temp)!=0,"temp path");
    auto root=std::filesystem::path(temp)/(L"ttpbrowser-download-tests-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount()));
    std::filesystem::create_directories(root);return root;
}
class ObservedStream final:public IStream {
    std::atomic<ULONG> refs_{1};
    Microsoft::WRL::ComPtr<IStream> inner_;
    std::atomic<unsigned>& releases_;
public:
    explicit ObservedStream(std::atomic<unsigned>& releases):releases_(releases){
        Require(SUCCEEDED(MemoryStream(&inner_)),"observed memory stream");
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=IID_IUnknown && iid!=IID_IStream && iid!=IID_ISequentialStream)return E_NOINTERFACE;
        *out=static_cast<IStream*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override{const auto count=--refs_;if(!count){++releases_;delete this;}return count;}
    HRESULT STDMETHODCALLTYPE Read(void* out,ULONG size,ULONG* got) override{return inner_->Read(out,size,got);}
    HRESULT STDMETHODCALLTYPE Write(const void* in,ULONG size,ULONG* done) override{return inner_->Write(in,size,done);}
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER offset,DWORD origin,ULARGE_INTEGER* position) override{return inner_->Seek(offset,origin,position);}
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER size) override{return inner_->SetSize(size);}
    HRESULT STDMETHODCALLTYPE CopyTo(IStream* target,ULARGE_INTEGER size,ULARGE_INTEGER* read,ULARGE_INTEGER* written) override{return inner_->CopyTo(target,size,read,written);}
    HRESULT STDMETHODCALLTYPE Commit(DWORD flags) override{return inner_->Commit(flags);}
    HRESULT STDMETHODCALLTYPE Revert() override{return inner_->Revert();}
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER offset,ULARGE_INTEGER count,DWORD flags) override{return inner_->LockRegion(offset,count,flags);}
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER offset,ULARGE_INTEGER count,DWORD flags) override{return inner_->UnlockRegion(offset,count,flags);}
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat,DWORD flags) override{return inner_->Stat(stat,flags);}
    HRESULT STDMETHODCALLTYPE Clone(IStream** out) override{return inner_->Clone(out);}
};
static void TestCancelRetryPersistence(const std::filesystem::path& root) {
    std::atomic<unsigned> calls{},signaled_on_open{};
    const auto file=root/L"queue.xml",destination=root/L"track.mp3";
    uint64_t id{};
    auto opener=[&](const std::wstring&,HANDLE cancellation,IStream** out,BOOL* persist)->HRESULT {
        *out=nullptr;*persist=TRUE;
        if(WaitForSingleObject(cancellation,0)!=WAIT_TIMEOUT)++signaled_on_open;
        if(++calls==1) {
            return WaitForSingleObject(cancellation,4000)==WAIT_OBJECT_0?HRESULT_FROM_WIN32(ERROR_CANCELLED):E_FAIL;
        }
        return MemoryStream(out);
    };
    {
        HttpDownloadQueue queue(file,opener);queue.Configure(1);
        Require(SUCCEEDED(queue.StorageResult()),"queue storage available");
        id=queue.Enqueue(L"https://example.invalid/song.mp3",L"Song",destination,false,L"Artist",L"tid-as-string");
        Require(id!=0,"enqueue");Wait([&]{return calls.load()==1;},"first callback");
        queue.Cancel(id);
        Wait([&]{return queue.Snapshot().front().state==DownloadState::canceled;},"cancel signaled event");
        Require(!std::filesystem::exists(destination),"canceled job not published");
        queue.Retry(id);
        Wait([&]{return queue.Snapshot().front().state==DownloadState::complete;},"retry completed");
        Require(signaled_on_open==0,"retry reset event before open");
        Require(std::filesystem::file_size(destination)>0,"complete destination");
        Require(queue.Snapshot().front().tid==L"tid-as-string","tid retained");
    }
    {
        HttpDownloadQueue queue(file,opener);queue.Configure(1);
        const auto rows=queue.Snapshot();Require(rows.size()==1 && rows[0].state==DownloadState::complete,"saved task restored");
        Require(rows[0].tid==L"tid-as-string","tid persisted");
        const auto entries=ttplayer::audio::ReadLegacyCacheList(file);
        Require(ttplayer::audio::ResolveLegacySavedFile(entries,L"https://example.invalid/song.mp3",{root})==destination,"host read-only lookup works");
        Require(queue.ClearHistory()==1 && queue.Snapshot().empty(),"clear history");
        Require(std::filesystem::exists(destination),"clear preserves downloaded file");
    }
}
static void TestRemoveAndShutdown(const std::filesystem::path& root) {
    std::atomic<unsigned> calls{},canceled{};
    auto opener=[&](const std::wstring&,HANDLE cancellation,IStream** out,BOOL* persist)->HRESULT{
        *out=nullptr;*persist=FALSE;++calls;
        if(WaitForSingleObject(cancellation,4000)==WAIT_OBJECT_0){++canceled;return HRESULT_FROM_WIN32(ERROR_CANCELLED);}
        return E_FAIL;
    };
    {
        HttpDownloadQueue queue(root/L"remove.xml",opener);queue.Configure(1);
        const auto id=queue.Enqueue(L"http://example.invalid/remove",L"Remove",root/L"remove.bin");
        Wait([&]{return calls.load()==1;},"remove callback running");
        Require(queue.Remove(id),"remove running job");
        Wait([&]{return canceled.load()==1 && queue.Snapshot().empty();},"remove cancels callback and clears record");
        Require(!std::filesystem::exists(root/L"remove.bin"),"remove never publishes");
        queue.Prefetch(L"http://example.invalid/prefetch");
        Wait([&]{return calls.load()==2;},"prefetch started");
        queue.Prefetch({});
        Wait([&]{return canceled.load()==2;},"prefetch replacement signals cancellation");
        queue.Enqueue(L"http://example.invalid/shutdown",L"Shutdown",root/L"shutdown.bin");
        Wait([&]{return calls.load()==3;},"shutdown job started");
    }
    Require(canceled==3,"destructor canceled and joined worker");
}
static void TestLeaseAndCallbackFailures(const std::filesystem::path& root) {
    std::atomic<unsigned> calls{};
    auto opener=[&](const std::wstring& url,HANDLE,IStream** out,BOOL* persist)->HRESULT {
        ++calls;*out=nullptr;*persist=TRUE;
        if(url.find(L"null")!=url.npos)return S_OK;
        if(url.find(L"throw")!=url.npos)throw std::runtime_error("host failed");
        return MemoryStream(out);
    };
    HttpDownloadQueue owner(root/L"lease.xml",opener);owner.Configure(1);
    {
        HttpDownloadQueue other(root/L"lease.xml",opener);other.Configure(1);
        Require(FAILED(other.StorageResult()),"second owner rejected by storage lease");
        Require(other.Enqueue(L"http://example.invalid/blocked",L"Blocked",root/L"blocked.bin")==0,"read-only queue refuses enqueue");
        other.Prefetch(L"http://example.invalid/blocked");
        Require(other.Snapshot().empty(),"read-only queue refuses speculative work");
    }
    owner.Enqueue(L"http://example.invalid/null",L"Null",root/L"null.bin");
    owner.Enqueue(L"http://example.invalid/throw",L"Throw",root/L"throw.bin");
    owner.Enqueue(L"http://example.invalid/good",L"Good",root/L"good.bin");
    Wait([&]{const auto rows=owner.Snapshot();return rows.size()==3 && rows[2].state==DownloadState::complete;},"worker survived bad host results");
    const auto rows=owner.Snapshot();
    Require(rows[0].state==DownloadState::failed && rows[0].result==E_UNEXPECTED,"null success stream rejected");
    Require(rows[1].state==DownloadState::failed && rows[1].result==E_FAIL,"host exception contained");
    Require(calls==3,"lease holder alone opens streams");
}
static void TestPrefetchPriorityAndRetention(const std::filesystem::path& root) {
    std::mutex order_mutex;std::vector<std::wstring> order;
    std::atomic<unsigned> predictions{},releases{};
    std::atomic<bool> release_first{};
    HANDLE duplicate{};
    auto opener=[&](const std::wstring& url,HANDLE cancellation,IStream** out,BOOL* persist)->HRESULT {
        *out=nullptr;*persist=TRUE;
        {std::lock_guard lock(order_mutex);order.push_back(url.substr(url.find_last_of(L'/')+1));}
        if(url.ends_with(L"prediction")) {
            if(++predictions==1) {
                if(WaitForSingleObject(cancellation,4000)!=WAIT_OBJECT_0)return E_FAIL;
                const auto deadline=std::chrono::steady_clock::now()+4s;
                while(!release_first && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);
                return HRESULT_FROM_WIN32(ERROR_CANCELLED);
            }
            Require(WaitForSingleObject(cancellation,0)==WAIT_TIMEOUT,"requeued prediction event reset");
            Require(DuplicateHandle(GetCurrentProcess(),cancellation,GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS)!=FALSE,"host duplicates cancellation handle");
            *out=new ObservedStream(releases);return S_OK;
        }
        return MemoryStream(out);
    };
    {
        HttpDownloadQueue queue(root/L"priority.xml",opener);queue.Configure(1);
        queue.Prefetch(L"http://example.invalid/prediction");
        Wait([&]{return predictions==1;},"prediction occupies slot");
        queue.Enqueue(L"http://example.invalid/first",L"First",root/L"first.bin");
        queue.Enqueue(L"http://example.invalid/second",L"Second",root/L"second.bin");
        release_first=true;
        Wait([&]{const auto rows=queue.Snapshot();return rows.size()==3 &&
            std::all_of(rows.begin(),rows.end(),[](const auto& row){return row.state==DownloadState::complete;});},"explicit jobs and resumed prefetch complete");
        {std::lock_guard lock(order_mutex);Require(order==std::vector<std::wstring>{L"prediction",L"first",L"second",L"prediction"},"explicit downloads preserve FIFO before prefetch");}
        Require(releases==0,"completed prediction retains stream for playback attachment");
        queue.Prefetch({});
        Require(releases==1 && queue.Snapshot().size()==2,"clearing prediction releases stream and removes row");
        Require(WaitForSingleObject(duplicate,0)==WAIT_OBJECT_0,"duplicated cancellation remains valid after job deletion");
        CloseHandle(duplicate);duplicate=nullptr;
        queue.Prefetch(L"http://example.invalid/first");
        std::this_thread::sleep_for(100ms);
        {std::lock_guard lock(order_mutex);Require(order.size()==4,"completed download avoids redundant prefetch");}
    }
    const auto saved=ttplayer::audio::ReadLegacyCacheList(root/L"priority.xml");
    Require(saved.size()==2,"prefetch never serialized into saved download catalog");
    {
        HttpDownloadQueue queue(root/L"nonpersistent.xml",[](const std::wstring&,HANDLE,IStream** out,BOOL* persist){*persist=FALSE;return MemoryStream(out);});
        queue.Configure(1);queue.Prefetch(L"http://example.invalid/no-cache");
        Wait([&]{const auto rows=queue.Snapshot();return rows.size()==1 && rows[0].state==DownloadState::failed;},"nonpersistent prefetch rejected");
        Require(queue.Snapshot()[0].result==HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED),"prefetch requires persistable producer");
    }
}
static void TestLegacyCacheDirectory(const std::filesystem::path& root) {
    const auto cache=root/L"cache",downloads=root/L"downloads",media=cache/L"old.mp3",catalog=root/L"legacy.xml";
    std::filesystem::create_directories(cache);std::filesystem::create_directories(downloads);
    {std::ofstream file(media,std::ios::binary);file<<"old cached media";}
    // Test paths are generated below the system temp root; escape XML explicitly.
    auto escaped=media.wstring();size_t pos{};while((pos=escaped.find(L'&',pos))!=escaped.npos){escaped.replace(pos,1,L"&amp;");pos+=5;}
    const auto xml=L"<?xml version=\"1.0\"?><cachelist version=\"2\"><items><item sourceurl=\"http://example.invalid/old\" save=\""+escaped+L"\"/></items></cachelist>";
    {std::ofstream file(catalog,std::ios::binary);file<<ttplayer::core::WideToUtf8(xml);}
    HttpDownloadQueue queue(root/L"import.xml",[](const std::wstring&,HANDLE,IStream**,BOOL*){return E_UNEXPECTED;});
    queue.Configure(1,cache);
    Require(queue.ImportLegacyList(catalog,downloads,false,false)==1,"legacy cache imported");
    const auto rows=queue.Snapshot();Require(rows.size()==1 && rows[0].state==DownloadState::complete && rows[0].destination==media,"configured cache root accepts verified saved file");
}
static void TestConcurrencyDecrease(const std::filesystem::path& root) {
    std::atomic<unsigned> first_calls{},second_calls{},second_canceled{};
    std::atomic<bool> first_release{};
    HttpDownloadQueue queue(root/L"concurrency.xml",[&](const std::wstring& url,HANDLE cancellation,IStream** out,BOOL* persist)->HRESULT {
        *out=nullptr;*persist=TRUE;
        if(url.ends_with(L"first")) {
            ++first_calls;
            const auto deadline=std::chrono::steady_clock::now()+4s;
            while(!first_release && std::chrono::steady_clock::now()<deadline && WaitForSingleObject(cancellation,0)==WAIT_TIMEOUT)std::this_thread::sleep_for(1ms);
            if(!first_release)return E_ABORT;
        } else if(++second_calls==1) {
            if(WaitForSingleObject(cancellation,4000)==WAIT_OBJECT_0){++second_canceled;return HRESULT_FROM_WIN32(ERROR_CANCELLED);}
            return E_FAIL;
        }
        if(WaitForSingleObject(cancellation,0)!=WAIT_TIMEOUT)return E_ABORT;
        return MemoryStream(out);
    });
    queue.Configure(2);
    queue.Enqueue(L"http://example.invalid/limit-first",L"First",root/L"limit-first.bin");
    queue.Enqueue(L"http://example.invalid/limit-second",L"Second",root/L"limit-second.bin");
    Wait([&]{return first_calls==1 && second_calls==1;},"two concurrent callbacks");
    queue.Configure(1);
    Wait([&]{return second_canceled==1;},"lower limit cancels excess active callback");
    Require(first_calls==1 && second_calls==1,"excess job waits for retained slot");
    first_release=true;
    Wait([&]{const auto rows=queue.Snapshot();return rows.size()==2 && rows[0].state==DownloadState::complete && rows[1].state==DownloadState::complete;},"excess job requeued with reset event");
    Require(first_calls==1 && second_calls==2,"concurrency decrease resumes only displaced job");
}
static void TestPausePersistence(const std::filesystem::path& root) {
    std::atomic<unsigned> calls{};
    const auto state=root/L"pause.xml";uint64_t id{};
    auto opener=[&](const std::wstring&,HANDLE cancel,IStream** out,BOOL* persist)->HRESULT {
        *out=nullptr;*persist=TRUE;if(++calls==1)return WaitForSingleObject(cancel,4000)==WAIT_OBJECT_0?HRESULT_FROM_WIN32(ERROR_CANCELLED):E_FAIL;
        Require(WaitForSingleObject(cancel,0)==WAIT_TIMEOUT,"pause retry clears event");return MemoryStream(out);
    };
    {
        HttpDownloadQueue queue(state,opener);queue.Configure(1);
        id=queue.Enqueue(L"http://example.invalid/pause",L"Pause",root/L"pause.bin");
        Wait([&]{return calls==1;},"pause task running");queue.Pause(id);
        Wait([&]{return queue.Snapshot()[0].state==DownloadState::paused;},"pause waits for worker exit");
        std::this_thread::sleep_for(100ms);Require(calls==1,"paused task does not reopen");
    }
    {
        HttpDownloadQueue queue(state,opener);queue.Configure(1);
        Require(queue.Snapshot()[0].state==DownloadState::paused,"paused state persists across close");
        std::this_thread::sleep_for(100ms);Require(calls==1,"restored paused task stays stopped");
        queue.Retry(queue.Snapshot()[0].id);
        Wait([&]{return queue.Snapshot()[0].state==DownloadState::complete;},"start resumes paused task");
    }
}
class ProgressStream final:public IStream {
    std::atomic<ULONG> refs_{1};HANDLE cancel_{};uint64_t received_{};std::atomic<bool>& halfway_;
public:
    ProgressStream(HANDLE cancel,std::atomic<bool>& halfway):halfway_(halfway){Require(DuplicateHandle(GetCurrentProcess(),cancel,GetCurrentProcess(),&cancel_,SYNCHRONIZE,FALSE,0)!=FALSE,"fixture duplicate event");}
    ~ProgressStream(){if(cancel_)CloseHandle(cancel_);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown && iid!=IID_IStream && iid!=IID_ISequentialStream)return E_NOINTERFACE;*out=static_cast<IStream*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}ULONG STDMETHODCALLTYPE Release() override{const auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Read(void* out,ULONG count,ULONG* got) override{*got=0;if(received_>=524288){halfway_=true;return WaitForSingleObject(cancel_,5000)==WAIT_OBJECT_0?HRESULT_FROM_WIN32(ERROR_CANCELLED):E_FAIL;}
        *got=static_cast<ULONG>(std::min<uint64_t>(count,524288-received_));std::memset(out,0x5a,*got);received_+=*got;return S_OK;}
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat,DWORD) override{*stat={};stat->type=STGTY_STREAM;stat->cbSize.QuadPart=1048576;return S_OK;}
    HRESULT STDMETHODCALLTYPE Write(const void*,ULONG,ULONG*) override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER,DWORD,ULARGE_INTEGER*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE CopyTo(IStream*,ULARGE_INTEGER,ULARGE_INTEGER*,ULARGE_INTEGER*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override{return S_OK;}HRESULT STDMETHODCALLTYPE Revert() override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD) override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override{return E_NOTIMPL;}
};
struct FixtureHost {
    std::atomic<bool> halfway{};
    unsigned submissions{},settings{};int mode{};std::vector<std::wstring> sources;
};
static HRESULT WINAPI FixtureOpen(void* context,const wchar_t*,HANDLE cancel,IStream** out,BOOL* persist){
    *persist=TRUE;*out=new ProgressStream(cancel,static_cast<FixtureHost*>(context)->halfway);return S_OK;
}
static HRESULT WINAPI FixtureSubmit(void* context,const ttp_browser_track* tracks,uint32_t count,int mode,int,int){
    auto& fixture=*static_cast<FixtureHost*>(context);++fixture.submissions;fixture.mode=mode;fixture.sources.clear();
    for(uint32_t i=0;i<count;++i)fixture.sources.emplace_back(tracks[i].source);return S_OK;
}
static LRESULT CALLBACK FixtureOwnerProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
    auto* fixture=reinterpret_cast<FixtureHost*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_COMMAND && LOWORD(wp)==2307 && fixture){++fixture->settings;return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
static void CaptureDownloadView(HWND view,const std::filesystem::path& path){
    RECT rect{};GetClientRect(view,&rect);HDC dc=GetDC(view),memory=CreateCompatibleDC(dc);BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=rect.right;info.bmiHeader.biHeight=-rect.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels{};HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);const auto old=SelectObject(memory,bitmap);
    SendMessageW(view,WM_PRINT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
    GdiFlush();
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+rect.right*rect.bottom*4;
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(&header),sizeof(header));file.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));file.write(static_cast<const char*>(pixels),rect.right*rect.bottom*4);
    Require(file.good(),"fixture screenshot saved");
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(view,dc);
}
static void TestNativeSkinAndCapture(const std::filesystem::path& root){
    ttpbrowser::DownloadSkin skin(nullptr);Require(skin.Image(ttpbrowser::DownloadSkin::Banner).size.cx==463 && skin.Image(ttpbrowser::DownloadSkin::Banner).size.cy==36,"original banner bytes");
    Require(skin.Image(ttpbrowser::DownloadSkin::Setting).size.cx==332 && skin.row_height==24,"four native settings frames and row height");
    LOGFONTW font{};GetObjectW(skin.list_font,sizeof(font),&font);Require(font.lfHeight==15,"native positive cell font height");
    const auto file=root/L"fixture.xml",saved=root/L"fixture-finished.mp3",saved2=root/L"fixture-second.mp3";
    for(const auto& path:{saved,saved2}){std::ofstream out(path,std::ios::binary);out<<std::string(1048576,'a');}
    auto xml=std::wstring(L"<?xml version=\"1.0\" encoding=\"UTF-8\"?><cachelist version=\"2\"><items>");
    auto add=[&](const wchar_t* key,const wchar_t* caption,const wchar_t* state,const std::filesystem::path& dest){
        xml+=L"<item sourceurl=\"http://fixture/"+std::wstring(key)+L"\" title=\"测试歌手 - "+caption+L"\" caption=\""+caption+L"\" state=\""+state+L"\" destination=\""+dest.wstring()+L"\" save=\""+(std::wstring(state)==L"complete"?dest.wstring():L"")+L"\"/>";};
    add(L"complete",L"已完成歌曲",L"complete",saved);add(L"complete-second",L"已完成第二首",L"complete",saved2);
    add(L"paused",L"已暂停歌曲",L"paused",root/L"paused.mp3");add(L"failed",L"失败歌曲",L"failed",root/L"failed.mp3");
    xml+=L"</items></cachelist>";{std::ofstream out(file,std::ios::binary);out<<ttplayer::core::WideToUtf8(xml);}
    FixtureHost fixture;auto state=file.wstring(),folder=root.wstring();ttp_browser_host host{};host.size=sizeof(host);host.version=TTP_BROWSER_ABI_VERSION;host.context=&fixture;host.open_stream=FixtureOpen;host.submit_tracks=FixtureSubmit;
    ttp_browser_config config{};config.size=sizeof(config);config.state_file=state.c_str();config.download_directory=folder.c_str();config.maximum_downloads=1;
    HWND parent=CreateWindowW(L"STATIC",L"native download fixture",WS_POPUP,0,0,470,424,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    SetWindowLongPtrW(parent,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&fixture));SetWindowLongPtrW(parent,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&FixtureOwnerProc));host.owner=parent;
    {
        ttpbrowser::Downloads downloads(host,config);const auto view=downloads.CreateView(parent);downloads.Resize({0,0,470,424});ShowWindow(view,SW_SHOW);
        const auto list=GetDlgItem(view,201);RECT list_rect{};GetWindowRect(list,&list_rect);MapWindowPoints(nullptr,view,reinterpret_cast<POINT*>(&list_rect),2);
        Require(list_rect.left==85 && list_rect.top==36,"native category 80 plus splitter 5 below banner 36");
        Require(GetDlgItem(view,2307)!=nullptr && GetDlgItem(view,207)==nullptr,"native settings exists and invented bottom status removed");
        ttp_browser_track running{L"http://fixture/running.mp3",L"测试歌手",L"正在下载歌曲",L"fixture"};uint64_t id{};Require(SUCCEEDED(downloads.Enqueue(running,&id)),"fixture running enqueue");
        Wait([&]{return fixture.halfway.load();},"fixture reaches real 50 percent");ttp_browser_track waiting{L"http://fixture/waiting.mp3",L"测试歌手",L"等待歌曲",L"fixture2"};downloads.Enqueue(waiting,&id);
        downloads.Refresh();const auto artifacts=std::filesystem::current_path()/L"downloads-ui";std::filesystem::create_directories(artifacts);
        CaptureDownloadView(view,artifacts/L"downloading-470x424.bmp");
        SendMessageW(view,WM_LBUTTONDOWN,0,MAKELPARAM(20,36+25+24+12));CaptureDownloadView(view,artifacts/L"downloaded-470x424.bmp");
        downloads.Resize({0,0,600,500});CaptureDownloadView(view,artifacts/L"downloaded-600x500.bmp");
        SendMessageW(view,WM_COMMAND,32974,0);SendMessageW(view,WM_COMMAND,32972,0);
        Require(fixture.submissions==1 && fixture.sources.size()==2 && fixture.mode==0,"multi-select play submits one ordered batch");
        for(const auto& source:fixture.sources)Require(std::filesystem::path(source).is_absolute() && std::filesystem::exists(source),"completed playback uses existing local file");
        SendMessageW(view,WM_COMMAND,32973,0);
        Require(fixture.submissions==2 && fixture.sources.size()==2 && fixture.mode==1,"multi-select add asks once for destination list");
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(2307,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(view,2307)));
        MSG message{};while(PeekMessageW(&message,parent,WM_COMMAND,WM_COMMAND,PM_REMOVE))DispatchMessageW(&message);
        Require(fixture.settings==1,"download settings forwards original 2307 command to host");
        std::wcout<<L"download skin screenshots: "<<artifacts.wstring()<<L'\n';
    }
    DestroyWindow(parent);
}
static HRESULT WINAPI HostOpen(void*,const wchar_t*,HANDLE cancel,IStream** out,BOOL* persist) {
    if(WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT)return E_ABORT;
    *persist=TRUE;return MemoryStream(out);
}
static void TestEmbeddedView(const std::filesystem::path& root) {
    auto state=(root/L"view.xml").wstring(),folder=root.wstring();
    ttp_browser_host host{};host.size=sizeof(host);host.version=TTP_BROWSER_ABI_VERSION;host.open_stream=HostOpen;
    ttp_browser_config config{};config.size=sizeof(config);config.state_file=state.c_str();config.download_directory=folder.c_str();config.maximum_downloads=2;
    const auto parent=CreateWindowExW(0,L"STATIC",L"download test",WS_OVERLAPPED,0,0,600,400,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Require(parent!=nullptr,"parent window");
    {
        ttpbrowser::Downloads downloads(host,config);
        const auto view=downloads.CreateView(parent);Require(view && GetParent(view)==parent,"downloads embedded child");
        Require((GetWindowLongPtrW(view,GWL_STYLE)&WS_CHILD)!=0,"not a modal top-level download window");
        downloads.Resize({10,12,510,312});RECT bounds{};GetWindowRect(view,&bounds);Require(bounds.right-bounds.left==500,"child resize");
        ttp_browser_track track{L"http://example.invalid/view.mp3",L"Artist",L"View",L"test-tid"};uint64_t id{};
        Require(SUCCEEDED(downloads.Enqueue(track,&id)) && id,"view enqueue");
        downloads.Refresh();
        Require(GetDlgItem(view,201)!=nullptr,"list view");
        DestroyWindow(view);
        Require(downloads.Window()==nullptr,"child destroy drops window handle");
        Require(downloads.CreateView(parent)!=nullptr,"child view recreated with same queue");
    }
    DestroyWindow(parent);
}
int wmain(){
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {
        const auto root=TestRoot();
        TestCancelRetryPersistence(root);TestRemoveAndShutdown(root);TestLeaseAndCallbackFailures(root);
        TestPrefetchPriorityAndRetention(root);TestLegacyCacheDirectory(root);TestConcurrencyDecrease(root);TestPausePersistence(root);TestEmbeddedView(root);TestNativeSkinAndCapture(root);
        // Every generated path is under the unique test directory.
        std::filesystem::remove_all(root);
        if(SUCCEEDED(com))CoUninitialize();
        std::cout<<"downloads: cancellation/retry, persistence, leases, exceptions, FIFO/prefetch, import, concurrency, shutdown, child UI passed\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(SUCCEEDED(com))CoUninitialize();return 1;}
}
