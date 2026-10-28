#pragma once
#include "media_library.h"
#include <memory>

namespace ttpbrowser {
class Downloads;
class MediaView final {
public:
    MediaView(media::Library&, const ttp_browser_host&, Downloads&);
    ~MediaView();
    HWND Create(HWND parent);
    HWND Window() const;
    void Resize(RECT);
    void Refresh();
    void ReloadProfiles();
    void Back();
    bool CanBack() const;
    bool Translate(MSG*);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
