#include "webview_backend.h"
#include "browser_bridge.h"
#include <atlbase.h>
#include <atlwin.h>
#include <atlhost.h>
#include <exdisp.h>
#include <exdispid.h>
#include <ocidl.h>
#include <oleidl.h>
#include <wrl/client.h>
#include <atomic>

namespace ttpbrowser {
using Microsoft::WRL::ComPtr;
namespace {
// ATL's CComObject<CAxHostWindow> calls _pAtlModule->Lock() during
// CreateControl. The optional DLL needs its own module; the EXE's WTL
// CAppModule belongs to a different image and cannot initialize this pointer.
class BrowserAtlModule final : public ATL::CAtlDllModuleT<BrowserAtlModule> {};
BrowserAtlModule browser_atl_module;

const VARIANT* Unwrap(const VARIANT* value) {
    for (unsigned i = 0; value && i < 8 && value->vt == (VT_BYREF | VT_VARIANT); ++i) value = value->pvarVal;
    return value;
}
std::wstring VariantText(const VARIANT& value) { auto* v = Unwrap(&value); return v && v->vt == VT_BSTR && v->bstrVal ? v->bstrVal : L""; }
class IeBackend final : public BrowserBackend, public std::enable_shared_from_this<IeBackend> {
    class Sink final : public IDispatch {
        std::atomic<ULONG> refs_{1};
        std::weak_ptr<IeBackend> owner_;
    public:
        explicit Sink(std::weak_ptr<IeBackend> owner) : owner_(std::move(owner)) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** value) override {
            if (!value) return E_POINTER; *value = nullptr;
            if (id != IID_IUnknown && id != IID_IDispatch && id != DIID_DWebBrowserEvents2) return E_NOINTERFACE;
            *value = static_cast<IDispatch*>(this); AddRef(); return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
        ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
        HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* n) override { if (!n) return E_POINTER; *n = 0; return S_OK; }
        HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return DISP_E_BADINDEX; }
        HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return DISP_E_UNKNOWNNAME; }
        HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID, LCID, WORD, DISPPARAMS* args, VARIANT*, EXCEPINFO*, UINT*) override {
            try { auto self = owner_.lock(); return self && !self->closed_ ? self->Event(id, args) : S_OK; }
            catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
            catch (...) { return E_FAIL; }
        }
    };
    ATL::CAxWindow host_window_;
    ComPtr<IWebBrowser2> browser_;
    ComPtr<IAxWinHostWindow> host_;
    ComPtr<IConnectionPoint> connection_;
    ComPtr<IDispatch> external_;
    ComPtr<IDispatch> sink_;
    DWORD cookie_{};
    bool closed_{}, failed_navigation_{};
    BrowserEvents events_;
    bool IsTop(IDispatch* dispatch) const {
        ComPtr<IUnknown> from, top;
        return dispatch && browser_ && SUCCEEDED(dispatch->QueryInterface(IID_PPV_ARGS(&from))) &&
            SUCCEEDED(browser_.As(&top)) && from.Get() == top.Get();
    }
    HRESULT Event(DISPID id, DISPPARAMS* args) {
        if (!args) return S_OK;
        if (id == DISPID_BEFORENAVIGATE2 && args->cArgs == 7) {
            const auto target = VariantText(args->rgvarg[5]);
            const auto* sender = Unwrap(&args->rgvarg[6]);
            const bool top = sender && sender->vt == VT_DISPATCH && IsTop(sender->pdispVal);
            const bool web = !HttpOrigin(target).empty();
            const bool trusted = web && events_.trusted && events_.trusted(target);
            // IE shares its OLE external object with documents in the control.
            // Reject unrelated frame navigation so it cannot inherit the native
            // bridge. Same-origin page frames share the top document's authority.
            if ((!web && target != L"about:blank") || (!top && target != L"about:blank" && !trusted)) {
                if (args->rgvarg[0].vt == (VT_BYREF | VT_BOOL)) *args->rgvarg[0].pboolVal = VARIANT_TRUE;
                return S_OK;
            }
            if (top) {
                failed_navigation_ = false;
                if (host_) host_->SetExternalDispatch(trusted ? external_.Get() : nullptr);
                if (events_.starting && web) events_.starting(target);
            }
        } else if (id == DISPID_DOCUMENTCOMPLETE && args->cArgs == 2 && args->rgvarg[1].vt == VT_DISPATCH && IsTop(args->rgvarg[1].pdispVal)) {
            const auto address = Source();
            if (address != L"about:blank" && !failed_navigation_ && events_.completed)
                events_.completed(true, L"");
        } else if (id == DISPID_NAVIGATEERROR && args->cArgs == 5 && args->rgvarg[4].vt == VT_DISPATCH && IsTop(args->rgvarg[4].pdispVal)) {
            failed_navigation_ = true;
            if (args->rgvarg[0].vt == (VT_BYREF | VT_BOOL)) *args->rgvarg[0].pboolVal = VARIANT_TRUE;
            const auto* code = Unwrap(&args->rgvarg[1]);
            const auto status = code && code->vt == VT_I4 ? code->lVal : 0;
            if (host_) host_->SetExternalDispatch(nullptr);
            if (events_.completed) events_.completed(false, L"网页导航失败（IE 错误 " + std::to_wstring(status) + L"）");
        } else if (id == DISPID_COMMANDSTATECHANGE && args->cArgs == 2) {
            static constexpr LONG kBack = 2, kForward = 1;
            const auto* command = Unwrap(&args->rgvarg[1]); const auto* value = Unwrap(&args->rgvarg[0]);
            if (command && value && command->vt == VT_I4 && value->vt == VT_BOOL) {
                if (command->lVal == kBack) back_ = value->boolVal != VARIANT_FALSE;
                if (command->lVal == kForward) forward_ = value->boolVal != VARIANT_FALSE;
                if (events_.history) events_.history(back_, forward_);
            }
        } else if (id == DISPID_NEWWINDOW2 || id == DISPID_NEWWINDOW3) {
            const UINT index = id == DISPID_NEWWINDOW3 ? 3 : 0;
            if (args->cArgs > index && args->rgvarg[index].vt == (VT_BYREF | VT_BOOL)) *args->rgvarg[index].pboolVal = VARIANT_TRUE;
            if (id == DISPID_NEWWINDOW3 && args->cArgs == 5) {
                const auto address = VariantText(args->rgvarg[0]);
                if (!HttpOrigin(address).empty()) Navigate(address);
            }
        } else if (id == DISPID_FILEDOWNLOAD && args->cArgs >= 1 && args->rgvarg[0].vt == (VT_BYREF | VT_BOOL)) {
            *args->rgvarg[0].pboolVal = VARIANT_TRUE;
            if (events_.completed) events_.completed(false, L"请使用页面的音乐下载按钮，下载任务由播放器管理。");
        }
        return S_OK;
    }
    bool back_{}, forward_{};
public:
    ~IeBackend() override { Shutdown(); }
    HRESULT Start(HWND parent, const RECT& rect, IDispatch* external, const std::wstring&, BrowserEvents events) override {
        events_ = std::move(events); external_ = external;
        if (!ATL::AtlAxWinInit()) return E_FAIL;
        RECT bounds = rect;
        if (!host_window_.Create(parent, bounds, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN))
            return HRESULT_FROM_WIN32(GetLastError());
        HRESULT hr = host_window_.CreateControl(L"Shell.Explorer.2");
        if (SUCCEEDED(hr)) hr = host_window_.QueryControl(browser_.GetAddressOf());
        if (SUCCEEDED(hr)) hr = host_window_.QueryHost(host_.GetAddressOf());
        if (FAILED(hr)) { Shutdown(); return hr; }
        ComPtr<IConnectionPointContainer> container;
        hr = browser_.As(&container);
        if (SUCCEEDED(hr)) hr = container->FindConnectionPoint(DIID_DWebBrowserEvents2, &connection_);
        sink_.Attach(new Sink(shared_from_this()));
        if (SUCCEEDED(hr)) hr = connection_->Advise(sink_.Get(), &cookie_);
        if (FAILED(hr)) { Shutdown(); return hr; }
        browser_->put_Silent(VARIANT_TRUE);
        if (events_.ready) events_.ready(L"IE / MSHTML");
        return S_OK;
    }
    HRESULT Navigate(const std::wstring& address) override {
        if (!browser_ || closed_ || HttpOrigin(address).empty()) return E_INVALIDARG;
        VARIANT url{}, headers{}, empty{}; url.vt = headers.vt = VT_BSTR;
        url.bstrVal = SysAllocString(address.c_str());
        const auto slash = address.rfind(L'/');
        const std::wstring referer = L"Referer: " + address.substr(0, slash + 1) + L"index.htm\r\n";
        headers.bstrVal = SysAllocString(referer.c_str());
        const HRESULT hr = url.bstrVal && headers.bstrVal ? browser_->Navigate2(&url, &empty, &empty, &empty, &headers) : E_OUTOFMEMORY;
        VariantClear(&url); VariantClear(&headers); return hr;
    }
    void Resize(const RECT& rect) override { if (host_window_.IsWindow()) host_window_.SetWindowPos(nullptr, &rect, SWP_NOZORDER | SWP_NOACTIVATE); }
    void Visible(bool show) override { if (host_window_.IsWindow()) host_window_.ShowWindow(show ? SW_SHOW : SW_HIDE); }
    void Back() override { if (browser_) browser_->GoBack(); }
    void Forward() override { if (browser_) browser_->GoForward(); }
    void Refresh() override { if (browser_) browser_->Refresh(); }
    void Stop() override { if (browser_) browser_->Stop(); }
    bool Translate(MSG* message) override { ComPtr<IOleInPlaceActiveObject> active;
        return browser_ && message && SUCCEEDED(browser_.As(&active)) && active->TranslateAccelerator(message) == S_OK; }
    std::wstring Source() const override { BSTR value{}; if (browser_) browser_->get_LocationURL(&value);
        std::wstring result = value ? value : L""; SysFreeString(value); return result; }
    void Shutdown() override {
        if (closed_) return; closed_ = true; events_ = {};
        if (host_) host_->SetExternalDispatch(nullptr);
        if (connection_ && cookie_) connection_->Unadvise(cookie_); cookie_ = 0; connection_.Reset(); sink_.Reset();
        if (browser_) browser_->Stop();
        if (host_window_.IsWindow()) host_window_.DestroyWindow();
        browser_.Reset(); host_.Reset(); external_.Reset();
        ATL::AtlAxWinTerm();
    }
};
}
std::shared_ptr<BrowserBackend> MakeIeBackend() { return std::make_shared<IeBackend>(); }
}
