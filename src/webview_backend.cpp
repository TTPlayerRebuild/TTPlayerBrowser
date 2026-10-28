#include "webview_backend.h"
#include "browser_bridge.h"
#include <WebView2.h>
#include <wrl.h>
#include <algorithm>

namespace ttpbrowser {
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
namespace {
const wchar_t kCompatibilityScript[] = LR"JS((function(){
  if (window !== window.top || !window.chrome || !chrome.webview) return;
  const native = chrome.webview.hostObjects.sync.ttlegacy;
  const player = {};
  const methods = ['AddSoundToPlay','AddSoundToList','AddBrowserHyper','ClearBrowserHyper',
    'DownSetupPack','BeginAddSongs','AddSong','EndAddSongs','GetSkinInfo','GetCacheInfo',
    'GetPlayerInfo','AddSoundToPlayByTid','AddSoundToListByTid','AddDownloadInfo','AddDownloadInfoByTid'];
  methods.forEach(function(name) { player[name] = function() {
    return native.ttplayer[name](...Array.prototype.slice.call(arguments));
  }; });
  const external = { AddRecomdSound: function() { return native.AddRecomdSound(...arguments); },
    ClearRecomdList: function() { return native.ClearRecomdList(); } };
  ['ttplayer','ttplayer2','ttplayer3','ttplayer4','ttplayer5','ttplayer6'].forEach(function(name) { external[name] = player; });
  try { Object.defineProperty(window, 'external', { get: function() { return external; }, configurable: false }); }
  catch (_) { try { Object.assign(window.external, external); } catch (_) {} }
})();)JS";
class WebViewBackend final : public BrowserBackend, public std::enable_shared_from_this<WebViewBackend> {
    HWND parent_{};
    RECT bounds_{};
    bool closed_{}, visible_{true}, navigating_{};
    UINT64 navigation_id_{};
    BrowserEvents events_;
    std::wstring pending_url_;
    ComPtr<IDispatch> external_;
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webview_;
    EventRegistrationToken navigation_{}, completed_{}, history_{}, popup_{}, permission_{}, download_{}, frame_navigation_{}, message_{};
    bool has_navigation_{}, has_completed_{}, has_history_{}, has_popup_{}, has_permission_{}, has_download_{};
    bool has_frame_navigation_{},has_message_{};
    std::wstring script_id_;
    void Failure(HRESULT hr) {
        if (closed_) return;
        wchar_t code[32]{}; swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(hr));
        if (events_.completed) events_.completed(false, std::wstring(L"WebView2 初始化失败（") + code + L"）");
    }
    void InitializeController(ICoreWebView2Controller* controller) {
        if (closed_) { if (controller) controller->Close(); return; }
        controller_ = controller;
        HRESULT hr = controller_->get_CoreWebView2(&webview_);
        if (FAILED(hr) || !webview_) { Failure(hr); return; }
        controller_->put_Bounds(bounds_); controller_->put_IsVisible(visible_);
        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(webview_->get_Settings(&settings))) {
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(TRUE);
            settings->put_IsWebMessageEnabled(events_.adapter_message?TRUE:FALSE);
        }
        std::weak_ptr<WebViewBackend> weak = shared_from_this();
        if(events_.adapter_message)has_message_=SUCCEEDED(webview_->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>([weak](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT{
                auto self=weak.lock();if(!self||self->closed_)return S_OK;
                LPWSTR source{},json{};args->get_Source(&source);args->TryGetWebMessageAsString(&json);
                if(source&&json&&self->events_.adapter_message)self->events_.adapter_message(source,json);
                CoTaskMemFree(source);CoTaskMemFree(json);return S_OK;
            }).Get(),&message_));
        has_navigation_ = SUCCEEDED(webview_->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
            [weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                auto self = weak.lock(); if (!self || self->closed_) return S_OK;
                LPWSTR uri{}; args->get_Uri(&uri); std::wstring target = uri ? uri : L""; CoTaskMemFree(uri);
                if(self->events_.intercept_download&&self->events_.intercept_download(self->Source(),target)){
                    args->put_Cancel(TRUE);return S_OK;
                }
                if (HttpOrigin(target).empty()) { args->put_Cancel(TRUE); return S_OK; }
                args->get_NavigationId(&self->navigation_id_);
                self->navigating_ = true;
                // The dispatch independently checks the current document URL at
                // every call. Remove the native object on an untrusted redirect.
                self->webview_->RemoveHostObjectFromScript(L"ttlegacy");
                if (self->external_ && self->events_.trusted && self->events_.trusted(target)) {
                    VARIANT value{}; value.vt = VT_DISPATCH; value.pdispVal = self->external_.Get();
                    self->webview_->AddHostObjectToScript(L"ttlegacy", &value);
                }
                if (self->events_.starting) self->events_.starting(target);
                return S_OK;
            }).Get(), &navigation_));
        if(events_.intercept_download)has_frame_navigation_=SUCCEEDED(webview_->add_FrameNavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>([weak](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{
                auto self=weak.lock();if(!self||self->closed_)return S_OK;
                LPWSTR uri{};args->get_Uri(&uri);
                if(uri&&self->events_.intercept_download(self->Source(),uri))args->put_Cancel(TRUE);
                CoTaskMemFree(uri);return S_OK;
            }).Get(),&frame_navigation_));
        has_completed_ = SUCCEEDED(webview_->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                auto self = weak.lock(); if (!self || self->closed_) return S_OK;
                UINT64 completed_id{}; args->get_NavigationId(&completed_id);
                if (completed_id != self->navigation_id_) return S_OK;
                self->navigating_ = false; BOOL success{}; args->get_IsSuccess(&success);
                COREWEBVIEW2_WEB_ERROR_STATUS status{}; args->get_WebErrorStatus(&status);
                ComPtr<ICoreWebView2NavigationCompletedEventArgs2> details;
                INT32 http_status{}; if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&details)))) details->get_HttpStatusCode(&http_status);
                if (http_status >= 400) success = FALSE;
                if (self->events_.completed) self->events_.completed(success != FALSE,
                    success ? L"" : http_status >= 400 ? L"服务器返回 HTTP " + std::to_wstring(http_status) :
                    L"网页导航失败（WebView2 错误 " + std::to_wstring(status) + L"）");
                return S_OK;
            }).Get(), &completed_));
        has_history_ = SUCCEEDED(webview_->add_HistoryChanged(Callback<ICoreWebView2HistoryChangedEventHandler>(
            [weak](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                auto self = weak.lock(); if (!self || self->closed_) return S_OK;
                BOOL back{}, forward{}; sender->get_CanGoBack(&back); sender->get_CanGoForward(&forward);
                if (self->events_.history) self->events_.history(back != FALSE, forward != FALSE); return S_OK;
            }).Get(), &history_));
        has_popup_ = SUCCEEDED(webview_->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [weak](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                args->put_Handled(TRUE); auto self = weak.lock(); if (!self || self->closed_) return S_OK;
                BOOL gesture{}; args->get_IsUserInitiated(&gesture); LPWSTR uri{}; args->get_Uri(&uri);
                if (gesture && uri && !HttpOrigin(uri).empty()) {
                    if(!self->events_.intercept_download||!self->events_.intercept_download(self->Source(),uri))self->Navigate(uri);
                }
                CoTaskMemFree(uri); return S_OK;
            }).Get(), &popup_));
        has_permission_ = SUCCEEDED(webview_->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK;
            }).Get(), &permission_));
        ComPtr<ICoreWebView2_4> webview4;
        if (SUCCEEDED(webview_.As(&webview4))) has_download_ = SUCCEEDED(webview4->add_DownloadStarting(
            Callback<ICoreWebView2DownloadStartingEventHandler>([weak](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                args->put_Cancel(TRUE); args->put_Handled(TRUE);
                auto self = weak.lock(); if (!self || self->closed_) return S_OK;
                ComPtr<ICoreWebView2DownloadOperation> operation; LPWSTR uri{};
                if (SUCCEEDED(args->get_DownloadOperation(&operation)) && SUCCEEDED(operation->get_Uri(&uri))) {
                    if(uri&&self->events_.intercept_download)self->events_.intercept_download(self->Source(),uri);
                    else if (uri && self->events_.trusted && self->events_.trusted(self->Source()) && self->events_.download)
                        self->events_.download(uri);
                    CoTaskMemFree(uri);
                }
                return S_OK;
            }).Get(), &download_));
        // Registration precedes navigation so scripts in the first document can
        // use the original synchronous external.ttplayer calling convention.
        if(!external_&&events_.adapter_script.empty()){
            if(events_.ready)events_.ready(L"WebView2");
            if(!pending_url_.empty())Navigate(pending_url_);
            return;
        }
        hr = webview_->AddScriptToExecuteOnDocumentCreated(events_.adapter_script.empty()?kCompatibilityScript:events_.adapter_script.c_str(),
            Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                [weak](HRESULT error, PCWSTR id) -> HRESULT {
                    auto self = weak.lock(); if (!self || self->closed_) return S_OK;
                    if (FAILED(error)) { self->Failure(error); return S_OK; }
                    if (id) self->script_id_ = id;
                    if (self->events_.ready) self->events_.ready(L"WebView2");
                    if (!self->pending_url_.empty()) self->Navigate(self->pending_url_);
                    return S_OK;
                }).Get());
        if (FAILED(hr)) Failure(hr);
    }
public:
    ~WebViewBackend() override { Shutdown(); }
    HRESULT Start(HWND parent, const RECT& bounds, IDispatch* external, const std::wstring& profile, BrowserEvents events) override {
        parent_ = parent; bounds_ = bounds; external_ = external; events_ = std::move(events);
        // Runtime callbacks can outlive Close(). Keep only module code resident;
        // Shutdown severs every owner callback and releases all instance state.
        HMODULE pinned{}; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&MakeWebViewBackend), &pinned);
        auto self = shared_from_this();
        return CreateCoreWebView2EnvironmentWithOptions(nullptr, profile.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [self](HRESULT error, ICoreWebView2Environment* environment) -> HRESULT {
                    if (self->closed_) return S_OK;
                    if (FAILED(error) || !environment) { self->Failure(FAILED(error) ? error : E_FAIL); return S_OK; }
                    self->environment_ = environment;
                    const HRESULT hr = environment->CreateCoreWebView2Controller(self->parent_,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [self](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                                if (self->closed_) { if (controller) controller->Close(); return S_OK; }
                                if (FAILED(result) || !controller) self->Failure(FAILED(result) ? result : E_FAIL);
                                else self->InitializeController(controller);
                                return S_OK;
                            }).Get());
                    if (FAILED(hr)) self->Failure(hr); return S_OK;
                }).Get());
    }
    HRESULT Navigate(const std::wstring& url) override {
        if (closed_ || HttpOrigin(url).empty()) return E_INVALIDARG;
        pending_url_ = url;
        if (!webview_) return S_OK;
        ComPtr<ICoreWebView2_2> webview2;
        ComPtr<ICoreWebView2Environment2> environment2;
        if (environment_ && SUCCEEDED(environment_.As(&environment2)) && SUCCEEDED(webview_.As(&webview2))) {
            const auto slash = url.rfind(L'/');
            const std::wstring headers = L"Referer: " + url.substr(0, slash + 1) + L"index.htm\r\n";
            ComPtr<ICoreWebView2WebResourceRequest> request;
            const HRESULT hr = environment2->CreateWebResourceRequest(url.c_str(), L"GET", nullptr, headers.c_str(), &request);
            if (SUCCEEDED(hr)) return webview2->NavigateWithWebResourceRequest(request.Get());
        }
        return webview_->Navigate(url.c_str());
    }
    void Resize(const RECT& rect) override { bounds_ = rect; if (controller_) controller_->put_Bounds(rect); }
    void Visible(bool show) override { visible_ = show; if (controller_) controller_->put_IsVisible(show); }
    void Back() override { if (webview_) webview_->GoBack(); }
    void Forward() override { if (webview_) webview_->GoForward(); }
    void Refresh() override { if (webview_) webview_->Reload(); }
    void Stop() override { if (webview_) webview_->Stop(); }
    bool Translate(MSG*) override { return false; }
    HRESULT PostJson(const std::wstring& json) override {return webview_?webview_->PostWebMessageAsJson(json.c_str()):E_UNEXPECTED;}
    std::wstring Source() const override { LPWSTR source{}; if (webview_) webview_->get_Source(&source);
        std::wstring result = source ? source : L""; CoTaskMemFree(source); return result; }
    void Shutdown() override {
        if (closed_) return; closed_ = true; events_ = {}; external_.Reset();
        if (webview_) {
            webview_->Stop(); webview_->RemoveHostObjectFromScript(L"ttlegacy");
            if (!script_id_.empty()) webview_->RemoveScriptToExecuteOnDocumentCreated(script_id_.c_str());
            if (has_navigation_) webview_->remove_NavigationStarting(navigation_);
            if (has_frame_navigation_) webview_->remove_FrameNavigationStarting(frame_navigation_);
            if (has_message_) webview_->remove_WebMessageReceived(message_);
            if (has_completed_) webview_->remove_NavigationCompleted(completed_);
            if (has_history_) webview_->remove_HistoryChanged(history_);
            if (has_popup_) webview_->remove_NewWindowRequested(popup_);
            if (has_permission_) webview_->remove_PermissionRequested(permission_);
            ComPtr<ICoreWebView2_4> webview4; if (has_download_ && SUCCEEDED(webview_.As(&webview4))) webview4->remove_DownloadStarting(download_);
        }
        if (controller_) controller_->Close(); webview_.Reset(); controller_.Reset(); environment_.Reset(); parent_ = nullptr;
    }
};
}
bool WebView2PlatformSupported() {
    OSVERSIONINFOEXW minimum{sizeof(minimum)};
    minimum.dwMajorVersion=6;minimum.dwMinorVersion=1;
    const auto mask=VerSetConditionMask(VerSetConditionMask(0,VER_MAJORVERSION,VER_GREATER_EQUAL),VER_MINORVERSION,VER_GREATER_EQUAL);
    return VerifyVersionInfoW(&minimum,VER_MAJORVERSION|VER_MINORVERSION,mask)!=FALSE;
}
std::shared_ptr<BrowserBackend> MakeWebViewBackend() {
    // Unsupported systems use IE/native without invoking the optional SDK.
    if(!WebView2PlatformSupported())return {};
    // The SDK loader is linked into this module; the browser Runtime remains
    // optional. Detection failure keeps the existing automatic IE fallback.
    LPWSTR installed{};
    const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &installed);
    CoTaskMemFree(installed);
    if (FAILED(hr)) return {};
    return std::make_shared<WebViewBackend>();
}
}
