#include "ttpbrowser.h"
#include "browser_window.h"
#include "downloads.h"
#include <memory>
#include <new>
#include <string>
#include <cstring>

namespace {
struct Module {
    DWORD thread{GetCurrentThreadId()};
    ttp_browser_host host{};
    ttp_browser_config config{};
    std::wstring runtime, state_file, download_directory, home, cache_directory;
    std::unique_ptr<ttpbrowser::Downloads> downloads;
    std::unique_ptr<ttpbrowser::BrowserWindow> browser;

    void CopyConfig(const ttp_browser_config& source) {
        runtime=source.runtime_directory ? source.runtime_directory : L"";
        state_file=source.state_file ? source.state_file : L"";
        download_directory=source.download_directory ? source.download_directory : L"";
        home=source.home_url && *source.home_url ? source.home_url : L"http://mini.qianqian.com/index.html";
        cache_directory=source.cache_directory ? source.cache_directory : L"";
        config=source;
        config.runtime_directory=runtime.c_str();
        config.state_file=state_file.c_str();
        config.download_directory=download_directory.c_str();
        config.home_url=home.c_str();
        config.cache_directory=cache_directory.c_str();
    }
};
bool ConfigValid(const ttp_browser_config* c) {
    return c && c->size>=sizeof(*c) && c->state_file && *c->state_file &&
        c->initial_state.size>=sizeof(ttp_browser_state) && c->engine<=2 &&
        (!c->home_url || !*c->home_url || !ttpbrowser::HttpOrigin(c->home_url).empty());
}
Module* Current(void* p) {
    auto* m=static_cast<Module*>(p);
    return m && m->thread==GetCurrentThreadId() ? m : nullptr;
}
template<class F> HRESULT Guard(F&& f) noexcept {
    try { return f(); } catch(const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch(...) { return E_FAIL; }
}
HRESULT WINAPI Create(const ttp_browser_host* h,const ttp_browser_config* c,void** out) {
    if(!out) return E_POINTER;
    *out=nullptr;
    if(!h || h->size<TTP_BROWSER_HOST_V1_SIZE || h->version!=TTP_BROWSER_ABI_VERSION ||
       !h->submit_tracks || !h->open_stream) return E_INVALIDARG;
    return Guard([&]() -> HRESULT {
        if(!ConfigValid(c))return E_INVALIDARG;
        auto m=std::make_unique<Module>();
        // Never read beyond an older host's allocation, nor copy a partial
        // callback pointer when the supplied size ends inside the extension.
        std::memcpy(&m->host,h,TTP_BROWSER_HOST_V1_SIZE);
        if(h->size>=offsetof(ttp_browser_host,adjust_window_rect)+sizeof(h->adjust_window_rect))
            m->host.adjust_window_rect=h->adjust_window_rect;
        if(h->size>=offsetof(ttp_browser_host,resolve_associated_lyric)+sizeof(h->resolve_associated_lyric))
            m->host.resolve_associated_lyric=h->resolve_associated_lyric;
        m->host.size=sizeof(m->host);m->CopyConfig(*c);
        m->downloads=std::make_unique<ttpbrowser::Downloads>(m->host,m->config);
        m->browser=std::make_unique<ttpbrowser::BrowserWindow>(m->host,m->config,*m->downloads);
        const auto hr=m->browser->Configure(m->config);if(FAILED(hr))return hr;
        *out=m.release();return S_OK;
    });
}
void WINAPI Destroy(void* p) {
    if(auto* m=Current(p)) { try { delete m; } catch(...) {} }
}
HRESULT WINAPI Configure(void* p,const ttp_browser_config* c) {
    auto* m=Current(p);if(!m) return E_INVALIDARG;
    return Guard([&]() -> HRESULT {
        if(!ConfigValid(c))return E_INVALIDARG;
        // Queue persistence ownership cannot be silently moved while workers run.
        if(m->state_file!=c->state_file)return E_INVALIDARG;
        m->CopyConfig(*c);const auto hr=m->downloads->Configure(m->config);if(FAILED(hr))return hr;
        return m->browser->Configure(m->config);});
}
HRESULT WINAPI SetSkin(void* p,const ttp_browser_skin* s) {
    auto* m=Current(p);if(!m || !s || s->size<sizeof(*s)) return E_INVALIDARG;
    return Guard([&]() -> HRESULT {return m->browser->SetSkin(*s);});
}
HRESULT WINAPI Show(void* p,uint32_t view) {
    auto* m=Current(p);if(!m || view>TTP_BROWSER_VIEW_HIDE) return E_INVALIDARG;
    return Guard([&]() -> HRESULT {return m->browser->Show(view);});
}
HWND WINAPI Window(void* p) {auto* m=Current(p);return m?m->browser->Window():nullptr;}
BOOL WINAPI Translate(void* p,MSG* msg) {
    auto* m=Current(p);if(!m || !msg) return FALSE;
    try{return m->browser->Translate(msg);}catch(...){return FALSE;}
}
HRESULT WINAPI State(void* p,ttp_browser_state* s) {
    auto* m=Current(p);if(!m || !s || s->size<sizeof(*s)) return E_INVALIDARG;
    return Guard([&]() -> HRESULT {return m->browser->GetState(s);});
}
HRESULT WINAPI Enqueue(void* p,const ttp_browser_track* song,uint64_t* id) {
    if(id)*id=0;auto* m=Current(p);if(!m || !song || !id) return E_INVALIDARG;
    return Guard([&]() -> HRESULT {return m->downloads->Enqueue(*song,id);});
}
HRESULT WINAPI Prefetch(void* p,const wchar_t* source) {
    auto* m=Current(p);if(!m) return E_INVALIDARG;
    return Guard([&]() -> HRESULT {return m->downloads->Prefetch(source?source:L"");});
}
HRESULT WINAPI External(void* p,IDispatch** out) {
    if(!out)return E_POINTER;*out=nullptr;auto* m=Current(p);if(!m)return E_INVALIDARG;
    return Guard([&]() -> HRESULT {return m->browser->ExternalDispatch(out);});
}
const ttp_browser_api api={sizeof(ttp_browser_api),TTP_BROWSER_ABI_VERSION,
    Create,Destroy,Configure,SetSkin,Show,Window,Translate,State,Enqueue,Prefetch,External};
}
extern "C" const ttp_browser_api* WINAPI ttp_browser_get_api(uint32_t version) {
    return version==TTP_BROWSER_ABI_VERSION ? &api : nullptr;
}
