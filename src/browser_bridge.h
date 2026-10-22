#pragma once
#include "ttpbrowser.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ttpbrowser {
struct BrowserTrack {
    std::wstring source, artist, title, tid;
    ttp_browser_track Borrow() const { return {source.c_str(), artist.c_str(), title.c_str(), tid.c_str()}; }
};
struct BridgeContext {
    bool alive{true};
    bool accept_recommendations{};
    uint64_t document_generation{1};
    std::function<bool()> allowed;
    std::function<HRESULT(std::vector<BrowserTrack>, int, int, int, bool, uint64_t)> submit;
    std::function<HRESULT(uint32_t, uint32_t, BSTR*)> query;
    std::function<HRESULT(std::wstring, std::wstring, std::wstring, uint64_t)> setup;
    std::function<HRESULT(std::wstring, std::wstring, uint64_t)> hyperlink;
    std::vector<BrowserTrack> recommendations;
    std::function<void()> recommendations_changed;
};
// Each external object has its own legacy Begin/Add/End staging transaction.
HRESULT CreateBrowserDispatch(std::shared_ptr<BridgeContext>, bool origin_scoped, IDispatch**);
std::wstring HttpOrigin(const std::wstring&);
bool IsMediaSource(const std::wstring&);
}
