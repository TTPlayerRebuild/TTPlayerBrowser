#pragma once
#include "ttpbrowser.h"
#include <memory>
#include <string>

namespace ttpbrowser {
// Queue ownership is independent of the child view and the active music page.
// All methods other than the host's open_stream callback run on the owner UI thread.
class Downloads {
public:
    Downloads(const ttp_browser_host& host, const ttp_browser_config& config);
    ~Downloads();
    Downloads(const Downloads&) = delete;
    Downloads& operator=(const Downloads&) = delete;
    HRESULT Configure(const ttp_browser_config& config);
    HRESULT Enqueue(const ttp_browser_track& track, uint64_t* task_id);
    HRESULT EnqueueFile(const std::wstring& url,const std::wstring& filename,uint64_t* task_id);
    HRESULT Prefetch(const wchar_t* url);
    HWND CreateView(HWND parent);
    void Resize(const RECT& bounds);
    void Refresh();
    HWND Window() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ttpbrowser
