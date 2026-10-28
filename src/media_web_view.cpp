#include "media_web_view.h"
#include "media_view.h"
#include "webview_backend.h"
#include "downloads.h"
#include "download_page.h"
#include <WebView2.h>
#include <wrl.h>
#include <wincrypt.h>
#include <gdiplus.h>
#include <atomic>
#include <future>
#include <chrono>
#include <set>

namespace ttpbrowser {
using namespace media;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
namespace {
constexpr wchar_t page_url[]=L"https://library.ttpbrowser.invalid/index.html";
constexpr wchar_t window_class[]=L"TTPBrowser.PersonalLibrary.v2";
constexpr UINT fallback_message=WM_APP+21;
HINSTANCE Module(){HMODULE h{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Module),&h);return h;}
void Require(bool valid,const wchar_t* message){if(!valid)throw Error{E_INVALIDARG,message};}
std::wstring Field(const Json& j,const char* key,size_t limit=4096){auto s=Wide(j[key].String());Require(s.size()<=limit&&s.find(L'\0')==s.npos,L"输入内容过长或无效");return s;}
std::string Bool(bool v){return v?"true":"false";}
std::string ProfilesJson(const Library& library){
    std::string result="[";bool first=true;unsigned index=0;const auto active=library.ActiveProfile();
    for(const auto& p:library.Profiles()){
        if(!first)result+=',';first=false;
        result+="{\"id\":"+Quote(p.id)+",\"active\":"+Bool(index++==active)+",\"name\":"+Quote(p.name)+",\"provider\":"+std::to_string(static_cast<unsigned>(p.provider))+",\"root\":"+Quote(p.root)+"}";
    }return result+"]";
}
std::string PageJson(const Page& page,uint64_t token){
    std::string result="{\"token\":"+std::to_string(token)+",\"more\":"+Bool(page.more)+",\"description\":"+Quote(page.description)+",\"items\":[";
    bool first=true;for(const auto& i:page.items){if(!first)result+=',';first=false;
        result+="{\"kind\":"+std::to_string(static_cast<unsigned>(i.kind))+",\"id\":"+Quote(i.id)+",\"title\":"+Quote(i.title)+",\"artist\":"+Quote(i.artist)+",\"album\":"+Quote(i.album)+",\"duration\":"+std::to_string(i.duration)+",\"size\":"+std::to_string(i.size)+",\"cover\":"+Bool(!i.cover.empty())+"}";
    }return result+"]}";
}
std::string ImageJson(const std::string& bytes){
    const char* mime=nullptr;
    if(bytes.starts_with("\x89PNG\r\n\x1a\n"))mime="image/png";
    else if(bytes.size()>3&&static_cast<unsigned char>(bytes[0])==255&&static_cast<unsigned char>(bytes[1])==216)mime="image/jpeg";
    else if(bytes.starts_with("GIF87a")||bytes.starts_with("GIF89a"))mime="image/gif";
    else if(bytes.size()>12&&bytes.starts_with("RIFF")&&bytes.substr(8,4)=="WEBP")mime="image/webp";
    if(!mime)return "\"\"";DWORD size{};
    if(!CryptBinaryToStringA(reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&size))return "\"\"";
    std::string encoded(size,'\0');if(!CryptBinaryToStringA(reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,encoded.data(),&size))return "\"\"";
    encoded.resize(size);while(!encoded.empty()&&!encoded.back())encoded.pop_back();return std::string("\"data:")+mime+";base64,"+encoded+"\"";
}
class PageDispatch final : public IDispatch {
    std::atomic<ULONG> refs_{1};std::function<void(const std::wstring&)> receive_;
public:
    explicit PageDispatch(std::function<void(const std::wstring&)> fn):receive_(std::move(fn)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_IDispatch)return E_NOINTERFACE;*out=static_cast<IDispatch*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override {auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* n) override {if(!n)return E_POINTER;*n=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT,LCID,ITypeInfo**) override {return DISP_E_BADINDEX;}
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID,LPOLESTR* names,UINT n,LCID,DISPID* ids) override {if(n!=1||!names||!ids||_wcsicmp(names[0],L"send"))return DISP_E_UNKNOWNNAME;ids[0]=1;return S_OK;}
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id,REFIID,LCID,WORD flags,DISPPARAMS* args,VARIANT*,EXCEPINFO*,UINT*) override {
        if(id!=1||!(flags&DISPATCH_METHOD)||!args||args->cArgs!=1||args->rgvarg[0].vt!=VT_BSTR)return DISP_E_TYPEMISMATCH;
        if(SysStringLen(args->rgvarg[0].bstrVal)>65536)return E_INVALIDARG;
        try{receive_(std::wstring(args->rgvarg[0].bstrVal,SysStringLen(args->rgvarg[0].bstrVal)));return S_OK;}catch(...){return E_FAIL;}
    }
};
std::wstring IePageUrl(){wchar_t path[32768]{};GetModuleFileNameW(Module(),path,32768);return std::wstring(L"res://")+path+L"/302";}
ComPtr<IStream> Bytes(const void* data,ULONG size){ComPtr<IStream> stream;if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)))return {};ULONG written{};LARGE_INTEGER zero{};
    if(FAILED(stream->Write(data,size,&written))||written!=size||FAILED(stream->Seek(zero,STREAM_SEEK_SET,nullptr)))return {};return stream;}
std::string IeCover(const std::string& bytes){
    // IE8 limits the whole data URI to 32 KiB. Base64 adds roughly one third.
    // Keep ordinary small images unchanged; larger artwork needs a thumbnail.
    if(bytes.size()<=24000)return bytes;
    struct GraphicsSession {
        ULONG_PTR token{};
        GraphicsSession(){Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&token,&input,nullptr);}
        ~GraphicsSession(){if(token)Gdiplus::GdiplusShutdown(token);}
    } graphics;
    if(!graphics.token)return {};
    auto input=Bytes(bytes.data(),static_cast<ULONG>(bytes.size()));if(!input)return {};
    Gdiplus::Bitmap original(input.Get());const auto width=original.GetWidth(),height=original.GetHeight();
    if(original.GetLastStatus()!=Gdiplus::Ok||!width||!height)return {};
    Gdiplus::Bitmap thumbnail(96,96,PixelFormat24bppRGB);
    {
        Gdiplus::Graphics canvas(&thumbnail);canvas.Clear(Gdiplus::Color(255,234,243,248));
        canvas.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        const double scale=96.0/std::max(width,height);
        const int w=std::max(1,static_cast<int>(width*scale)),h=std::max(1,static_cast<int>(height*scale));
        if(canvas.DrawImage(&original,(96-w)/2,(96-h)/2,w,h)!=Gdiplus::Ok)return {};
    }
    auto output=Bytes(nullptr,0);if(!output)return {};
    const CLSID jpeg={0x557cf401,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    ULONG quality=80;Gdiplus::EncoderParameters parameters{};parameters.Count=1;
    parameters.Parameter[0]={Gdiplus::EncoderQuality,1,Gdiplus::EncoderParameterValueTypeLong,&quality};
    if(thumbnail.Save(output.Get(),&jpeg,&parameters)!=Gdiplus::Ok)return {};
    STATSTG stat{};if(FAILED(output->Stat(&stat,STATFLAG_NONAME))||!stat.cbSize.QuadPart||stat.cbSize.QuadPart>24000)return {};
    std::string result(static_cast<size_t>(stat.cbSize.QuadPart),'\0');LARGE_INTEGER zero{};ULONG read{};
    if(FAILED(output->Seek(zero,STREAM_SEEK_SET,nullptr))||FAILED(output->Read(result.data(),static_cast<ULONG>(result.size()),&read))||read!=result.size())return {};
    return result;
}
}
struct MediaSurface::State : std::enable_shared_from_this<State> {
    Library& library;ttp_browser_host host;Downloads& downloads;std::wstring profile;
    HWND window{};uint32_t engine{};bool disable_gpu{},closed{},ready{},can_back{},failed{},fallback_pending{},ie_attempt{};DWORD started{};UINT64 navigation_id{};
    std::unique_ptr<MediaView> native;std::shared_ptr<BrowserBackend> ie;std::wstring ie_url;
    ComPtr<ICoreWebView2Environment> environment;ComPtr<ICoreWebView2Controller> controller;ComPtr<ICoreWebView2> web;
    struct Result {uint64_t id{},epoch{};std::string op,json;std::wstring error;Page page;Profile profile;};
    struct Job {std::shared_ptr<std::atomic<bool>> canceled;std::future<Result> future;};
    std::vector<Job> jobs;uint64_t epoch{},page_token{};Page current_page;Profile current_profile;
    State(Library& l,const ttp_browser_host& h,Downloads& d,const wchar_t* state,uint32_t e):library(l),host(h),downloads(d),engine(e){
        auto folder=std::filesystem::path(state?state:L"").parent_path();if(folder.empty())folder=std::filesystem::temp_directory_path()/L"TTPlayer";
        profile=(folder/L"PersonalLibrary.WebView2").wstring();
    }
    ~State(){Close();for(auto& j:jobs)if(j.future.valid())j.future.wait();}
    void CancelJobs(){++epoch;for(auto& j:jobs)*j.canceled=true;current_page={};++page_token;}
    void Close(){if(closed)return;closed=true;CancelJobs();if(controller)controller->Close();web.Reset();controller.Reset();environment.Reset();if(ie)ie->Shutdown();ie.reset();native.reset();}
    void Fallback(){if(!closed&&!native&&!fallback_pending){failed=true;fallback_pending=true;ready=false;if(window)PostMessageW(window,fallback_message,0,0);}}
    void UseIe(){if(closed)return;failed=true;fallback_pending=false;ready=false;ie_attempt=true;CancelJobs();
        if(controller)controller->Close();web.Reset();controller.Reset();environment.Reset();started=GetTickCount();
        ie=MakeIeBackend();ie_url=IePageUrl();std::weak_ptr<State> weak=shared_from_this();ComPtr<IDispatch> external;
        external.Attach(new PageDispatch([weak](const std::wstring& json){auto s=weak.lock();if(s&&!s->closed&&s->ready&&s->ie&&s->ie->Source()==s->ie_url)s->Receive(json);}));
        BrowserEvents events;events.local_document=ie_url;
        events.completed=[weak](bool ok,const std::wstring&){auto s=weak.lock();if(!s||s->closed||!s->ie)return;if(!ok||s->ie->Source()!=s->ie_url){s->Fallback();return;}
            s->ready=true;auto posted=s->ie->PostJson(L"{\"event\":\"ready\"}");if(FAILED(posted))s->Fallback();};
        RECT r{};GetClientRect(window,&r);auto hr=ie->Start(window,r,external.Get(),L"",std::move(events));if(SUCCEEDED(hr)){hr=ie->Navigate(ie_url);}if(FAILED(hr)){UseNative();return;}Layout();
    }
    void UseNative(){if(closed||native)return;fallback_pending=false;CancelJobs();if(ie)ie->Shutdown();ie.reset();if(controller)controller->Close();web.Reset();controller.Reset();environment.Reset();ready=false;
        native=std::make_unique<MediaView>(library,host,downloads);native->Create(window);Layout();ShowWindow(native->Window(),SW_SHOW);InvalidateRect(window,nullptr,TRUE);}
    void Layout(){if(!window)return;RECT r{};GetClientRect(window,&r);if(controller){controller->put_Bounds(r);controller->NotifyParentWindowPositionChanged();}if(native)native->Resize(r);if(ie)ie->Resize(r);}
    void Post(const std::string& json){if(!closed&&ready){if(web)web->PostWebMessageAsJson(Wide(json).c_str());else if(ie)ie->PostJson(Wide(json));}}
    void Reply(uint64_t id,const std::string& value,const std::wstring& error={}){Post("{\"id\":"+std::to_string(id)+",\"ok\":"+Bool(error.empty())+",\"data\":"+(error.empty()?value:"null")+",\"error\":"+Quote(error)+"}");}
    void Event(const char* name){Post(std::string("{\"event\":\"")+name+"\"}");}
    template<class F> void Run(uint64_t id,std::string op,F fn){
        Poll();Require(jobs.size()<8,L"正在处理请求，请稍后重试");auto cancel=std::make_shared<std::atomic<bool>>(false);auto e=epoch;
        jobs.push_back({cancel,std::async(std::launch::async,[id,op,e,cancel,fn]() mutable {Result r;r.id=id;r.op=op;r.epoch=e;
            try{fn(r,Cancel([cancel]{return cancel->load();}));}catch(const Error& ex){r.error=ex.message;}catch(...){r.error=L"请求失败，请检查服务配置";}return r;})});
    }
    void Poll(){for(size_t i=0;i<jobs.size();){if(jobs[i].future.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){++i;continue;}
        auto r=jobs[i].future.get();jobs.erase(jobs.begin()+i);if(closed)continue;
        if(r.epoch!=epoch){Reply(r.id,"null",L"请求已取消");continue;}
        if(r.op=="browse"&&r.error.empty()){current_page=std::move(r.page);current_profile=std::move(r.profile);r.json=PageJson(current_page,++page_token);}
        if(r.op=="playContainer"&&r.error.empty())try{
            std::vector<BrowserTrack> tracks;for(const auto& item:r.page.items)tracks.push_back({Source(r.profile,item,false),item.artist,item.title,r.profile.id+L":"+item.id});Dispatch(tracks,"play");r.json="true";
        }catch(const Error& ex){r.error=ex.message;}catch(...){r.error=L"播放失败";}
        Reply(r.id,r.json.empty()?"null":r.json,r.error);
    }}
    Profile GetProfile(const Json& request){auto list=library.Profiles();if(request["profileId"].kind==Json::string){const auto id=Field(request,"profileId",80);for(const auto& p:list)if(p.id==id)return p;throw Error{E_INVALIDARG,L"媒体来源已删除，请重新选择"};}auto index=request["profile"].Integer();Require(index<list.size(),L"无效媒体来源");return list[static_cast<size_t>(index)];}
    Item GetItem(const Json& request){Require(request["token"].Integer()==page_token,L"列表已更新，请重新选择歌曲");auto index=request["index"].Integer();Require(index<current_page.items.size(),L"请选择有效条目");return current_page.items[static_cast<size_t>(index)];}
    void Submit(const Json& request,const std::string& op){
        Require(host.media_source_version&&IsWindowVisible(window),L"音乐窗当前不可用");Require(request["token"].Integer()==page_token,L"列表已更新，请重新选择歌曲");
        const auto& indices=request["indices"].items;Require(!indices.empty()&&indices.size()<=100,L"请先选择歌曲");std::set<size_t> unique;std::vector<BrowserTrack> tracks;
        for(const auto& entry:indices){const auto n=entry.Integer();Require(n<current_page.items.size(),L"无效歌曲条目");const auto& item=current_page.items[static_cast<size_t>(n)];
            if(unique.insert(static_cast<size_t>(n)).second&&item.kind==Kind::Song)tracks.push_back({Source(current_profile,item,op=="download"),item.artist,item.title,current_profile.id+L":"+item.id});}
        Dispatch(tracks,op);
    }
    void Dispatch(const std::vector<BrowserTrack>& tracks,const std::string& op){
        Require(host.media_source_version&&IsWindowVisible(window),L"音乐窗当前不可用");
        Require(!tracks.empty(),L"请先打开专辑或目录，再选择歌曲");HRESULT hr=S_OK;
        if(op=="download"){for(const auto& t:tracks){auto borrowed=t.Borrow();uint64_t task{};auto result=downloads.Enqueue(borrowed,&task);if(FAILED(result))hr=result;}}
        else {std::vector<ttp_browser_track> borrowed;for(const auto& t:tracks)borrowed.push_back(t.Borrow());hr=host.submit_tracks?host.submit_tracks(host.context,borrowed.data(),static_cast<uint32_t>(borrowed.size()),0,1,op=="play"?1:0):E_NOTIMPL;}
        if(hr==HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)&&op=="download"){if(host.owner)PostMessageW(host.owner,WM_COMMAND,0x903,0);throw Error{hr,L"请先在下载设置中选择保存目录，然后再次下载"};}
        if(FAILED(hr))throw Error{hr,L"操作失败，请检查播放器与下载设置"};
    }
    void Receive(const std::wstring& message){
        uint64_t id{};try{
            Require(message.size()<=65536,L"请求过大");const auto j=ttplayer::core::JsonReader(Utf8(message),65536).Read();id=j["id"].Integer();Require(id>0&&id<=9007199254740991ULL,L"请求标识无效");const auto op=j["op"].String();
            if(op=="profiles"){Reply(id,ProfilesJson(library));return;}
            if(op=="downloadPage"){const auto page=GetDownloadPage(host);Reply(id,page.name.empty()?"null":"{\"name\":"+Quote(page.name)+"}");return;}
            if(op=="openDownloadPage"){Require(IsWindowVisible(window)&&!GetDownloadPage(host).name.empty(),L"曲包下载组件不可用");Reply(id,"true");OpenDownloadPage(window);return;}
            if(op=="selectSource"){Require(IsWindowVisible(window),L"音乐窗当前不可用");library.SelectProfile(Field(j,"profileId",80));CancelJobs();Reply(id,"true");Event("profilesChanged");return;}
            if(op=="navigation"){can_back=j["canBack"].True();InvalidateRect(GetParent(window),nullptr,FALSE);Reply(id,"null");return;}
            if(op=="settings"){Require(IsWindowVisible(window),L"音乐窗当前不可用");Require(host.open_media_settings!=nullptr,L"请更新主程序，在选项 → 网络连接 → 音乐窗中设置媒体来源");const auto hr=host.open_media_settings(host.context);if(FAILED(hr))throw Error{hr,L"无法打开选项"};Reply(id,"true");return;}
            if(op=="browse"){
                auto p=GetProfile(j);Query q;auto view=j["view"].Integer();Require(view<=static_cast<unsigned>(View::Search),L"无效分类");q.view=static_cast<View>(view);q.id=Field(j,"path");q.text=Field(j,"text",256);auto page=j["page"].Integer();Require(page<10000,L"页码超出范围");q.page=static_cast<unsigned>(page);
                CancelJobs();auto* l=&library;Run(id,op,[l,p,q](Result& r,const Cancel& c){r.page=l->Browse(p,q,c);r.profile=p;});return;
            }
            if(op=="playContainer"){
                const auto item=GetItem(j);const auto p=current_profile;auto* l=&library;
                Require(host.media_source_version&&IsWindowVisible(window),L"音乐窗当前不可用");
                Run(id,op,[l,p,item](Result& r,const Cancel& c){r.page.items=l->Playable(p,item,c);r.profile=p;});return;
            }
            if(op=="cover"||op=="details"){
                const auto item=GetItem(j);const auto p=current_profile;auto* l=&library;const bool legacy=ie!=nullptr;
                Run(id,op,[l,p,item,op,legacy](Result& r,const Cancel& c){std::string cover;std::wstring lyrics;
                    if(!item.cover.empty())try{cover=l->Cover(p,item.cover,c);}catch(const Error&){}
                    if(legacy)cover=IeCover(cover);
                    if(op=="details"&&item.kind==Kind::Song)try{lyrics=l->Lyrics(p,item,c);}catch(const Error&){}
                    r.json="{\"image\":"+ImageJson(cover)+",\"lyrics\":"+Quote(lyrics)+"}";
                });return;
            }
            if(op=="play"||op=="add"||op=="download"){Submit(j,op);Reply(id,"true");return;}
            throw Error{E_INVALIDARG,L"不支持此操作"};
        }catch(const Error& e){if(id)Reply(id,"null",e.message);}catch(...){if(id)Reply(id,"null",L"请求格式不正确");}
    }
    bool Trusted(){LPWSTR url{};if(!web)return false;web->get_Source(&url);const bool valid=url&&std::wstring(url)==page_url;CoTaskMemFree(url);return valid;}
    void Initialize(ICoreWebView2Controller* value){
        if(closed||failed){if(value)value->Close();return;}controller=value;if(FAILED(controller->get_CoreWebView2(&web))){Fallback();return;}
        ComPtr<ICoreWebView2Settings> settings;if(SUCCEEDED(web->get_Settings(&settings))){settings->put_IsWebMessageEnabled(TRUE);settings->put_AreHostObjectsAllowed(FALSE);settings->put_AreDevToolsEnabled(FALSE);settings->put_AreDefaultContextMenusEnabled(FALSE);settings->put_IsStatusBarEnabled(FALSE);settings->put_AreDefaultScriptDialogsEnabled(FALSE);}
        std::weak_ptr<State> weak=shared_from_this();EventRegistrationToken token{};
        web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([weak](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{
            LPWSTR url{};args->get_Uri(&url);const bool valid=url&&std::wstring(url)==page_url;CoTaskMemFree(url);auto s=weak.lock();if(!valid||!s||s->closed||s->failed)args->put_Cancel(TRUE);else{args->get_NavigationId(&s->navigation_id);s->ready=false;s->CancelJobs();}return S_OK;}).Get(),&token);
        web->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{args->put_Cancel(TRUE);return S_OK;}).Get(),&token);
        web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([weak](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT{auto s=weak.lock();if(!s||s->closed||s->failed)return S_OK;UINT64 id{};args->get_NavigationId(&id);if(id!=s->navigation_id)return S_OK;BOOL ok{};args->get_IsSuccess(&ok);if(!ok||!s->Trusted())s->Fallback();else{s->ready=true;s->Event("ready");}return S_OK;}).Get(),&token);
        web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([weak](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT{
            auto s=weak.lock();if(!s||s->closed||s->failed||!s->ready||!s->Trusted())return S_OK;LPWSTR source{};args->get_Source(&source);const bool trusted=source&&std::wstring(source)==page_url;CoTaskMemFree(source);if(!trusted)return S_OK;
            LPWSTR json{};if(SUCCEEDED(args->get_WebMessageAsJson(&json))&&json){try{s->Receive(json);}catch(...){}CoTaskMemFree(json);}return S_OK;}).Get(),&token);
        web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* a)->HRESULT{a->put_Handled(TRUE);return S_OK;}).Get(),&token);
        web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs* a)->HRESULT{a->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);return S_OK;}).Get(),&token);
        ComPtr<ICoreWebView2_4> web4;if(SUCCEEDED(web.As(&web4)))web4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>([](ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs* a)->HRESULT{a->put_Cancel(TRUE);a->put_Handled(TRUE);return S_OK;}).Get(),&token);
        web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([weak](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*)->HRESULT{if(auto s=weak.lock())s->Fallback();return S_OK;}).Get(),&token);
        web->AddWebResourceRequestedFilter(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        web->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>([weak](ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs* args)->HRESULT{
            auto s=weak.lock();if(!s||s->closed||!s->environment)return S_OK;ComPtr<ICoreWebView2WebResourceRequest> request;args->get_Request(&request);if(!request)return E_FAIL;
            LPWSTR uri{},method{};request->get_Uri(&uri);request->get_Method(&method);const bool valid=uri&&std::wstring(uri)==page_url&&method&&std::wstring(method)==L"GET";CoTaskMemFree(uri);CoTaskMemFree(method);
            ComPtr<IStream> stream;int status=403;
            if(valid){const auto resource=FindResourceW(Module(),MAKEINTRESOURCEW(301),RT_RCDATA);if(resource){auto data=LoadResource(Module(),resource);stream=Bytes(LockResource(data),SizeofResource(Module(),resource));if(stream)status=200;}}
            ComPtr<ICoreWebView2WebResourceResponse> response;
            const auto hr=s->environment->CreateWebResourceResponse(stream.Get(),status,status==200?L"OK":L"Forbidden",
                L"Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data:; connect-src 'none'; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'\r\n",&response);
            if(SUCCEEDED(hr))args->put_Response(response.Get());return hr;}).Get(),&token);
        Layout();controller->put_IsVisible(IsWindowVisible(window));if(FAILED(web->Navigate(page_url)))Fallback();
    }
    void Start(){
        started=GetTickCount();if(engine==1||!WebView2PlatformSupported()){UseIe();return;}
        // Avoid launching a Runtime error dialog for an unusable profile path.
        try{std::filesystem::create_directories(profile);if(!std::filesystem::is_directory(profile)){UseIe();return;}}catch(...){UseIe();return;}
        LPWSTR version{};auto hr=GetAvailableCoreWebView2BrowserVersionString(nullptr,&version);CoTaskMemFree(version);if(FAILED(hr)){UseIe();return;}
        // Runtime completion handlers may run after the owner closes.
        HMODULE pinned{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Module),&pinned);
        auto self=shared_from_this();hr=CreateWebViewEnvironment(profile,disable_gpu,Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([self](HRESULT error,ICoreWebView2Environment* env)->HRESULT{
            if(self->closed||self->failed)return S_OK;if(FAILED(error)||!env){self->Fallback();return S_OK;}self->environment=env;
            auto result=env->CreateCoreWebView2Controller(self->window,Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([self](HRESULT code,ICoreWebView2Controller* controller)->HRESULT{
                if(self->closed||self->failed){if(controller)controller->Close();return S_OK;}if(FAILED(code)||!controller)self->Fallback();else self->Initialize(controller);return S_OK;}).Get());if(FAILED(result))self->Fallback();return S_OK;}).Get());if(FAILED(hr))Fallback();
    }
    static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        auto* s=reinterpret_cast<State*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(msg==WM_NCCREATE){s=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);s->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
        if(!s)return DefWindowProcW(hwnd,msg,wp,lp);try{switch(msg){
        case WM_SIZE:s->Layout();return 0;
        case WM_SHOWWINDOW:if(s->ie)s->ie->Visible(wp!=0);if(s->controller)s->controller->put_IsVisible(wp!=0);break;
        case WM_TIMER:s->Poll();if(!s->ready&&!s->native&&GetTickCount()-s->started>15000)s->Fallback();return 0;
        case fallback_message:if(s->ie_attempt)s->UseNative();else s->UseIe();return 0;
        case WM_PAINT:{PAINTSTRUCT p{};HDC dc=BeginPaint(hwnd,&p);FillRect(dc,&p.rcPaint,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));if(!s->ready&&!s->native){RECT r{};GetClientRect(hwnd,&r);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,L"正在打开个人媒体库…",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}EndPaint(hwnd,&p);return 0;}
        case WM_DESTROY:KillTimer(hwnd,1);s->Close();return 0;
        case WM_NCDESTROY:s->window=nullptr;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);break;
        }}catch(...){s->Fallback();}return DefWindowProcW(hwnd,msg,wp,lp);
    }
};
MediaSurface::MediaSurface(Library& l,const ttp_browser_host& h,Downloads& d,const wchar_t* path,uint32_t engine):state_(std::make_shared<State>(l,h,d,path,engine)){}
MediaSurface::~MediaSurface(){state_->Close();if(state_->window)DestroyWindow(state_->window);for(auto& j:state_->jobs)if(j.future.valid())j.future.wait();}
HWND MediaSurface::Create(HWND parent){auto s=state_;if(s->window)return s->window;WNDCLASSW wc{};wc.hInstance=Module();wc.lpszClassName=window_class;wc.lpfnWndProc=State::Proc;wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    auto window=CreateWindowExW(WS_EX_CONTROLPARENT,window_class,L"个人媒体库",WS_CHILD|WS_CLIPCHILDREN,0,0,0,0,parent,nullptr,Module(),s.get());if(window){SetTimer(window,1,80,nullptr);s->Start();}return window;}
HWND MediaSurface::Window() const{return state_->window;}
void MediaSurface::Resize(RECT r){if(state_->window)MoveWindow(state_->window,r.left,r.top,r.right-r.left,r.bottom-r.top,TRUE);}
void MediaSurface::Refresh(){if(state_->native)state_->native->Refresh();else state_->Event("refresh");}
void MediaSurface::Back(){if(state_->native)state_->native->Back();else state_->Event("back");}
bool MediaSurface::CanBack() const{return state_->native?state_->native->CanBack():state_->can_back;}
void MediaSurface::ReloadProfiles(){if(state_->native)state_->native->ReloadProfiles();else {state_->CancelJobs();state_->Event("profilesChanged");}}
bool MediaSurface::IsIe() const{return state_->ready&&state_->ie!=nullptr;}
bool MediaSurface::IsWebView() const{return state_->ready&&state_->web&&!state_->closed;}
bool MediaSurface::Translate(MSG* m){return state_->native?state_->native->Translate(m):state_->ie?state_->ie->Translate(m):false;}
void MediaSurface::Configure(uint32_t engine,bool disable_gpu){if(state_->engine==engine&&state_->disable_gpu==disable_gpu)return;auto& s=state_;s->engine=engine;s->disable_gpu=disable_gpu;if(s->window){auto parent=GetParent(s->window);RECT r{};GetWindowRect(s->window,&r);MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&r),2);bool visible=IsWindowVisible(s->window)!=FALSE;
    auto next=std::make_shared<State>(s->library,s->host,s->downloads,L"",engine);next->profile=s->profile;next->disable_gpu=disable_gpu;s->Close();DestroyWindow(s->window);for(auto& j:s->jobs)if(j.future.valid())j.future.wait();state_=next;Create(parent);Resize(r);ShowWindow(state_->window,visible?SW_SHOW:SW_HIDE);}}
}
