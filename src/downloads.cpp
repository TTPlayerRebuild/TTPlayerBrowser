#include "downloads.h"
#include "download_skin.h"
#include "download_file_actions.h"
#include <map>
#include <set>
#include <shellapi.h>
#include <shlobj.h>
#include "ttplayer/audio/http_download_queue.h"
#include <algorithm>
#include <array>
#include <commctrl.h>
#include <commdlg.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>
#include <windowsx.h>

namespace ttpbrowser {
namespace {
using ttplayer::audio::DownloadState;
using ttplayer::audio::HttpDownload;
using ttplayer::audio::HttpDownloadQueue;
constexpr wchar_t kClass[] = L"TTPBrowser.Downloads.v1",kListClass[]=L"TTPBrowser.DownloadList.v1";
constexpr UINT kRefreshTimer = 1,kScrollRepeatTimer=2;
enum : int {kList=201,kLink=202,kSettings=2307,kStart=32960,kPause=32961,kRemoveTask=32962,kSelectTasks=32963,kRemove=32966,kDeleteFile=32967,kRetry=32968,kPlay=32972,kAdd=32973,kSelectFiles=32974,kBrowse=32975,kSendFolder=32980,kImport=40001,kClear=40002};
std::wstring Copy(const wchar_t* value) { return value ? value : L""; }
HRESULT ExceptionResult() noexcept {
    try { throw; }
    catch(const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch(const std::system_error& e) {
        return e.code().value() ? HRESULT_FROM_WIN32(e.code().value()) : E_FAIL;
    }
    catch(const std::invalid_argument&) { return E_INVALIDARG; }
    catch(...) { return E_FAIL; }
}
std::filesystem::path StateFile(const ttp_browser_config& config) {
    auto path=std::filesystem::path(Copy(config.state_file));
    if(path.empty()) path=std::filesystem::path(Copy(config.runtime_directory))/L"HttpDownloads"/L"cachelist.xml";
    return std::filesystem::absolute(path).lexically_normal();
}
HINSTANCE ThisModule() noexcept {
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&ThisModule),&module);
    return module;
}
void Fill(HDC dc,RECT r,COLORREF color){auto brush=CreateSolidBrush(color);FillRect(dc,&r,brush);DeleteObject(brush);}
void Text(HDC dc,RECT r,const std::wstring& text,COLORREF color,UINT flags=DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX){SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&r,flags);}
std::wstring Artist(const HttpDownload& row){const auto suffix=L" - "+row.display_title;return !row.display_title.empty() && row.entry.title.ends_with(suffix)?row.entry.title.substr(0,row.entry.title.size()-suffix.size()):std::wstring{};}
} // namespace

struct Downloads::Impl {
    ttp_browser_host host;
    std::filesystem::path state_file, folder, reported_recovery;
    bool artist_folders{}, replace{};
    std::unique_ptr<HttpDownloadQueue> queue;
    DownloadSkin skin;
    HWND window{},list{},link{},settings{},tooltip{};
    std::vector<HttpDownload> rows;
    std::vector<size_t> visible;
    std::set<uint64_t> selected;
    struct Rate {uint64_t bytes{};DWORD tick{};double speed{};};
    std::map<uint64_t,Rate> rates;
    int category{},category_width{80},scroll{},focus_row{-1},anchor{-1},sort_column{-1},hover_header{-1};
    bool descending{},split_drag{},thumb_drag{},split_moved{};
    int drag_y{},drag_scroll{},hot_scroll{},header_pressed{-1},wheel_remainder{};
    int scroll_repeat{},repeat_y{},split_start{},split_anchor{},saved_category_width{80};
    uint64_t pressed_play_id{};
    POINT last_list_point{-1,-1};
    std::wstring tooltip_text;
    HRESULT last_storage{S_OK};

    Impl(const ttp_browser_host& source,const ttp_browser_config& config):host(source),state_file(StateFile(config)),skin(source.resources) {
        if(!host.open_stream) throw std::invalid_argument("missing stream opener");
        // Capture the ABI value, never a view pointer. Queue shutdown joins all
        // calls before the host may invalidate its context or unload this DLL.
        queue=std::make_unique<HttpDownloadQueue>(state_file,
            [callback=host.open_stream,context=host.context](const std::wstring& url,HANDLE cancel,IStream** stream,BOOL* persist) {
                return callback(context,url.c_str(),cancel,stream,persist);
            });
        const auto result=Configure(config);
        if(FAILED(result)) throw std::invalid_argument("invalid download configuration");
    }
    ~Impl() {
        if(window && IsWindow(window)) DestroyWindow(window);
        // The last view unregisters its DLL-owned procedure before unload.
        // If another instance still owns a view, Windows keeps its class.
        UnregisterClassW(kListClass,ThisModule());
        UnregisterClassW(kClass,ThisModule());
    }

    HRESULT Configure(const ttp_browser_config& config) {
        try {
            if(config.size<sizeof(ttp_browser_config) || StateFile(config)!=state_file) return E_INVALIDARG;
            auto next_folder=std::filesystem::path(Copy(config.download_directory));
            if(!next_folder.empty()) next_folder=std::filesystem::absolute(next_folder).lexically_normal();
            folder=std::move(next_folder);artist_folders=config.artist_folders!=FALSE;replace=config.replace_files!=FALSE;
            // The cache root is used only to validate migrated saved files.
            // Live network/cache options stay in the host stream producer.
            auto cache=std::filesystem::path(Copy(config.cache_directory));
            if(!cache.empty())cache=std::filesystem::absolute(cache).lexically_normal();
            queue->Configure(config.maximum_downloads,std::move(cache));
            return S_OK;
        } catch(...) { return ExceptionResult(); }
    }
#include "downloads_ui.inc"

};

Downloads::Downloads(const ttp_browser_host& host,const ttp_browser_config& config):impl_(std::make_unique<Impl>(host,config)) {}
Downloads::~Downloads()=default;
HRESULT Downloads::Configure(const ttp_browser_config& config){return impl_->Configure(config);}
HRESULT Downloads::Enqueue(const ttp_browser_track& track,uint64_t* task_id) {
    if(task_id)*task_id=0;
    try {
        auto source=Copy(track.source);
        if(source.starts_with(L"https://ttpbrowser.invalid/v1/")) {
            const auto action=source.find(L"/play/",31);
            if(action!=source.npos)source.replace(action,6,L"/download/");
        }
        const auto url=ttplayer::audio::HttpDownloadKey(std::move(source));if(url.empty())return E_INVALIDARG;
        if(impl_->folder.empty())return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        const auto storage=impl_->queue->StorageResult();if(FAILED(storage))return storage;
        auto filename_title=Copy(track.title);
        if(url.starts_with(L"https://ttpbrowser.invalid/v1/")) {
            const auto extension=std::filesystem::path(url.substr(url.find_last_of(L'/')+1)).extension().wstring();
            if(!extension.empty() && filename_title.size()>extension.size() &&
                !_wcsicmp(filename_title.c_str()+filename_title.size()-extension.size(),extension.c_str()))
                filename_title.resize(filename_title.size()-extension.size());
        }
        auto destination=ttplayer::audio::HttpDownloadDestination(impl_->folder,url,std::move(filename_title),Copy(track.artist),impl_->artist_folders);
        const auto id=impl_->queue->Enqueue(url,Copy(track.title),std::move(destination),impl_->replace,Copy(track.artist),Copy(track.tid));
        if(!id)return E_FAIL;
        if(task_id)*task_id=id;
        impl_->Refresh();return S_OK;
    } catch(...) {return ExceptionResult();}
}
HRESULT Downloads::EnqueueFile(const std::wstring& source,const std::wstring& filename,uint64_t* task_id) {
    if(task_id)*task_id=0;
    try {
        const auto url=ttplayer::audio::HttpDownloadKey(source);
        if(url.empty()||filename.empty()||filename.size()>200||
            filename.find_first_of(L"\\/:*?\"<>|")!=filename.npos||
            std::any_of(filename.begin(),filename.end(),[](wchar_t c){return c<32;})||
            _wcsicmp(std::filesystem::path(filename).extension().c_str(),L".osz"))return E_INVALIDARG;
        if(impl_->folder.empty())return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
        const auto storage=impl_->queue->StorageResult();if(FAILED(storage))return storage;
        const auto id=impl_->queue->Enqueue(url,filename,impl_->folder/filename,impl_->replace,L"",L"",true);
        if(!id)return E_FAIL;if(task_id)*task_id=id;impl_->Refresh();return S_OK;
    }catch(...){return ExceptionResult();}
}
HRESULT Downloads::Prefetch(const wchar_t* url) {
    try {
        auto value=Copy(url);
        if(!value.empty() && ttplayer::audio::HttpDownloadKey(value).empty())return E_INVALIDARG;
        const auto storage=impl_->queue->StorageResult();if(!value.empty() && FAILED(storage))return storage;
        impl_->queue->Prefetch(std::move(value));return S_OK;
    } catch(...) {return ExceptionResult();}
}
HWND Downloads::CreateView(HWND parent){try{return impl_->CreateView(parent);}catch(...){return nullptr;}}
void Downloads::Resize(const RECT& bounds){if(impl_->window)MoveWindow(impl_->window,bounds.left,bounds.top,std::max(0L,bounds.right-bounds.left),std::max(0L,bounds.bottom-bounds.top),TRUE);}
void Downloads::Refresh(){try{impl_->Refresh();}catch(...){}}
HWND Downloads::Window() const noexcept{return impl_->window;}
} // namespace ttpbrowser
