#pragma once
#include <windows.h>
#include <oaidl.h>
#include <functional>
#include <memory>
#include <string>

namespace ttpbrowser {
struct BrowserEvents {
    std::function<void(const wchar_t*)> ready;
    std::function<void(const std::wstring&)> starting;
    std::function<void(bool, const std::wstring&)> completed;
    std::function<void(bool, bool)> history;
    std::function<bool(const std::wstring&)> trusted;
    std::function<void(const std::wstring&)> download;
    // Return true only when a component recognized and consumed a file link.
    std::function<bool(const std::wstring& page,const std::wstring& target)> intercept_download;
    std::wstring adapter_script;
    std::function<void(const std::wstring& page,const std::wstring& json)> adapter_message;
    // An exact embedded res: URL. Nonempty opts into a closed local document.
    std::wstring local_document;
};
class BrowserBackend {
public:
    virtual ~BrowserBackend() = default;
    virtual HRESULT Start(HWND, const RECT&, IDispatch*, const std::wstring&, BrowserEvents) = 0;
    virtual HRESULT Navigate(const std::wstring&) = 0;
    virtual void Resize(const RECT&) = 0;
    virtual void Visible(bool) = 0;
    virtual void Back() = 0;
    virtual void Forward() = 0;
    virtual void Refresh() = 0;
    virtual void Stop() = 0;
    virtual bool Translate(MSG*) = 0;
    virtual std::wstring Source() const = 0;
    virtual void Shutdown() = 0;
    virtual HRESULT PostJson(const std::wstring&) { return E_NOTIMPL; }
};
// The loader is statically linked. Creation requires an available Runtime;
// asynchronous failure is reported through completed(false, ...).
bool WebView2PlatformSupported();
std::shared_ptr<BrowserBackend> MakeWebViewBackend();
std::shared_ptr<BrowserBackend> MakeIeBackend();
}
