#include "browser_bridge.h"
#include <wininet.h>
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <new>

namespace ttpbrowser {
std::wstring HttpOrigin(const std::wstring& url) {
    if (url.empty() || url.size() > 8192 || std::any_of(url.begin(), url.end(), [](wchar_t c) { return c <= L' ' || c == L'\\'; })) return {};
    URL_COMPONENTSW parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!InternetCrackUrlW(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts) ||
        (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) ||
        !parts.dwHostNameLength || parts.dwUserNameLength || parts.dwPasswordLength) return {};
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::transform(host.begin(), host.end(), host.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return (parts.nScheme == INTERNET_SCHEME_HTTPS ? L"https://" : L"http://") + host + L":" + std::to_wstring(parts.nPort);
}
bool IsMediaSource(const std::wstring& url) { return !HttpOrigin(url).empty(); }
namespace {
constexpr size_t kMaxTracks = 10000;
const wchar_t* const kNames[]{L"", L"AddSoundToPlay", L"AddSoundToList", L"AddBrowserHyper", L"ClearBrowserHyper",
    L"DownSetupPack", L"BeginAddSongs", L"AddSong", L"EndAddSongs", L"GetSkinInfo", L"GetCacheInfo",
    L"GetPlayerInfo", L"AddSoundToPlayByTid", L"AddSoundToListByTid", L"AddDownloadInfo", L"AddDownloadInfoByTid"};
const UINT kArguments[]{0,4,4,2,0,3,0,3,3,1,3,1,5,5,4,5};
struct OwnedVariant {
    VARIANT value{};
    ~OwnedVariant() { VariantClear(&value); }
};
HRESULT StringArg(DISPPARAMS* params, UINT index, std::wstring& value) {
    const auto& arg = params->rgvarg[params->cArgs - index - 1];
    if (arg.vt == VT_EMPTY || arg.vt == VT_NULL) { value.clear(); return S_OK; }
    OwnedVariant converted;
    auto& copy = converted.value;
    HRESULT hr = VariantChangeType(&copy, const_cast<VARIANT*>(&arg), 0, VT_BSTR);
    if (FAILED(hr)) return DISP_E_TYPEMISMATCH;
    const UINT length = SysStringLen(copy.bstrVal);
    if (length > 8192 || (copy.bstrVal && wcslen(copy.bstrVal) != length)) return E_INVALIDARG;
    value.assign(copy.bstrVal ? copy.bstrVal : L"", length); return S_OK;
}
HRESULT IntArg(DISPPARAMS* params, UINT index, int& value) {
    VARIANT copy{}; HRESULT hr = VariantChangeType(&copy, &params->rgvarg[params->cArgs - index - 1], 0, VT_I4);
    if (FAILED(hr)) return DISP_E_TYPEMISMATCH;
    value = copy.lVal; VariantClear(&copy); return S_OK;
}
class Dispatch final : public IDispatch {
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<BridgeContext> context_;
    bool outer_, scoped_;
    IDispatch* inner_{};
    std::vector<BrowserTrack> staged_;
    uint64_t staged_generation_{};
    HRESULT Validate() const {
        if (!context_ || !context_->alive) return RPC_E_DISCONNECTED;
        if (scoped_ && (!context_->allowed || !context_->allowed())) return E_ACCESSDENIED;
        return S_OK;
    }
    HRESULT Append(DISPPARAMS* p, bool with_tid) {
        if (staged_.size() >= kMaxTracks) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        BrowserTrack track;
        HRESULT hr = StringArg(p, 0, track.source);
        if (SUCCEEDED(hr)) hr = StringArg(p, 1, track.artist);
        if (SUCCEEDED(hr)) hr = StringArg(p, 2, track.title);
        if (SUCCEEDED(hr) && with_tid) hr = StringArg(p, 3, track.tid);
        if (FAILED(hr)) return hr;
        if (!IsMediaSource(track.source)) return E_INVALIDARG;
        staged_.push_back(std::move(track)); return S_OK;
    }
    HRESULT Commit(int a, int b, int c, bool download) {
        if (staged_.empty()) return S_FALSE;
        if (!context_->submit) return E_NOTIMPL;
        auto tracks = std::move(staged_); staged_.clear();
        return context_->submit(std::move(tracks), a, b, c, download, scoped_ ? context_->document_generation : 0);
    }
public:
    Dispatch(std::shared_ptr<BridgeContext> c, bool scoped, bool outer) : context_(std::move(c)), outer_(outer), scoped_(scoped) {}
    ~Dispatch() { if (inner_) inner_->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDispatch) return E_NOINTERFACE;
        *result = static_cast<IDispatch*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override { if (!count) return E_POINTER; *count = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return DISP_E_BADINDEX; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID iid, LPOLESTR* names, UINT count, LCID, DISPID* ids) override {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if (!names || !ids) return E_POINTER;
        for (UINT i = 0; i < count; ++i) {
            ids[i] = DISPID_UNKNOWN;
            if (!names[i]) return E_INVALIDARG;
            if (outer_) {
                if (!_wcsicmp(names[i], L"AddRecomdSound")) ids[i] = 1;
                else if (!_wcsicmp(names[i], L"ClearRecomdList")) ids[i] = 2;
                else if (!_wcsicmp(names[i], L"ttplayer") || (!_wcsnicmp(names[i], L"ttplayer", 8) &&
                    names[i][8] >= L'2' && names[i][8] <= L'6' && !names[i][9])) ids[i] = 3;
            } else for (DISPID id = 1; id < static_cast<DISPID>(std::size(kNames)); ++id)
                if (!_wcsicmp(names[i], kNames[id])) { ids[i] = id; break; }
            if (ids[i] == DISPID_UNKNOWN) return DISP_E_UNKNOWNNAME;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID iid, LCID, WORD flags, DISPPARAMS* p,
        VARIANT* result, EXCEPINFO*, UINT*) override {
        try { return InvokeImpl(id, iid, flags, p, result); }
        catch (const std::bad_alloc&) { staged_.clear(); if (result) VariantClear(result); return E_OUTOFMEMORY; }
        catch (...) { staged_.clear(); if (result) VariantClear(result); return E_FAIL; }
    }
    HRESULT InvokeImpl(DISPID id, REFIID iid, WORD flags, DISPPARAMS* p, VARIANT* result) {
        if (result) VariantInit(result);
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if (scoped_ && staged_generation_ != context_->document_generation) {
            staged_.clear(); staged_generation_ = context_->document_generation;
        }
        const HRESULT access = Validate(); if (FAILED(access)) return access;
        if (!p || p->cNamedArgs) return DISP_E_NONAMEDARGS;
        if (p->cArgs && !p->rgvarg) return E_POINTER;
        if (outer_) {
            if (id == 3 && (flags & DISPATCH_PROPERTYGET)) {
                if (!result) return E_POINTER;
                if (p->cArgs) return DISP_E_BADPARAMCOUNT;
                if (!inner_) inner_ = new (std::nothrow) Dispatch(context_, scoped_, false);
                if (!inner_) return E_OUTOFMEMORY;
                result->vt = VT_DISPATCH; result->pdispVal = inner_; inner_->AddRef(); return S_OK;
            }
            if (!(flags & DISPATCH_METHOD)) return DISP_E_MEMBERNOTFOUND;
            if (id == 2) { if (p->cArgs) return DISP_E_BADPARAMCOUNT; return S_OK; }
            if (id == 1) {
                if (!context_->accept_recommendations) return S_FALSE;
                // 0040B86C forwards the three BSTR arguments to 0040B8EB.
                // Preserve the old callable interface, but recommendations are
                // intentionally not implemented: no playlist, menu or playback.
                if (p->cArgs != 3 || !p->rgvarg) return DISP_E_BADPARAMCOUNT;
                HRESULT hr = Append(p, false); if (FAILED(hr)) return hr;
                staged_.clear();return S_FALSE;
            }
            return DISP_E_MEMBERNOTFOUND;
        }
        if (!(flags & DISPATCH_METHOD) || id < 1 || id > 15) return DISP_E_MEMBERNOTFOUND;
        if (p->cArgs != kArguments[id]) return DISP_E_BADPARAMCOUNT;
        if (p->cArgs && !p->rgvarg) return E_POINTER;
        if (id == 6) { staged_.clear(); return S_OK; }
        if (id == 7) return Append(p, false);
        if (id == 10) return E_FAIL; // Exact legacy Invoke failure for unimplemented GetCacheInfo.
        if (id == 9 || id == 11) {
            int selector{}; HRESULT hr = IntArg(p, 0, selector); if (FAILED(hr)) return hr;
            if (!result || !context_->query) return E_POINTER;
            // 0040B588 and main dispatch 0046450C return a null BSTR for
            // unsupported selectors; legacy feature probes rely on no exception.
            if (selector != 1 && selector != 2) { result->vt = VT_BSTR; result->bstrVal = nullptr; return S_OK; }
            BSTR text{}; hr = context_->query(id == 11 ? 1u : 2u, static_cast<uint32_t>(selector), &text);
            if (FAILED(hr)) { SysFreeString(text); return hr; }
            result->vt = VT_BSTR; result->bstrVal = text ? text : SysAllocString(L"");
            return result->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        if (id == 3 || id == 4) {
            std::wstring text, url; HRESULT hr = S_OK;
            if (id == 3) { hr = StringArg(p, 0, text); if (SUCCEEDED(hr)) hr = StringArg(p, 1, url);
                // Reconstruction compatibility policy: accept either argument
                // order when exactly one string is an HTTP(S) address.
                if (!IsMediaSource(url) && IsMediaSource(text)) std::swap(text, url);
                if (SUCCEEDED(hr) && !IsMediaSource(url)) hr = E_INVALIDARG; }
            if (SUCCEEDED(hr)) hr = context_->hyperlink ? context_->hyperlink(std::move(text), std::move(url), scoped_ ? context_->document_generation : 0) : E_NOTIMPL;
            return hr;
        }
        if (id == 5) {
            std::wstring a, b, c; HRESULT hr = StringArg(p, 0, a);
            if (SUCCEEDED(hr)) hr = StringArg(p, 1, b); if (SUCCEEDED(hr)) hr = StringArg(p, 2, c);
            return FAILED(hr) ? hr : context_->setup ? context_->setup(std::move(a), std::move(b), std::move(c), scoped_ ? context_->document_generation : 0) : E_NOTIMPL;
        }
        if (id == 8) { int a{}, b{}, c{}; HRESULT hr = IntArg(p, 0, a);
            if (SUCCEEDED(hr)) hr = IntArg(p, 1, b); if (SUCCEEDED(hr)) hr = IntArg(p, 2, c);
            return FAILED(hr) ? hr : Commit(a, b, c, false); }
        const bool tid = id == 12 || id == 13 || id == 15;
        int commit{}; HRESULT hr = IntArg(p, tid ? 4 : 3, commit); if (FAILED(hr)) return hr;
        hr = Append(p, tid); if (FAILED(hr) || !commit) return hr;
        const bool play = id == 1 || id == 12;
        return Commit(play ? 0 : 1, play ? 1 : 0, play ? 1 : 0, id == 14 || id == 15);
    }
};
}
HRESULT CreateBrowserDispatch(std::shared_ptr<BridgeContext> context, bool scoped, IDispatch** result) {
    if (!result) return E_POINTER;
    *result = new (std::nothrow) Dispatch(std::move(context), scoped, true);
    return *result ? S_OK : E_OUTOFMEMORY;
}
}
