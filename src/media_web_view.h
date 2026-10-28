#pragma once
#include "media_library.h"
#include <memory>

namespace ttpbrowser {
class Downloads;
// Embedded pages use WebView2, then IE/MSHTML, then native MediaView.
// Credentials and service requests remain in Library for every renderer.
class MediaSurface final {
public:
    MediaSurface(media::Library&, const ttp_browser_host&, Downloads&, const wchar_t* state_file, uint32_t engine);
    ~MediaSurface();
    HWND Create(HWND);
    HWND Window() const;
    void Resize(RECT);
    void Refresh();
    void ReloadProfiles();
    bool IsIe() const;
    void Back();
    bool CanBack() const;
    bool IsWebView() const;
    bool Translate(MSG*);
    void Configure(uint32_t engine, bool disable_gpu);
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
