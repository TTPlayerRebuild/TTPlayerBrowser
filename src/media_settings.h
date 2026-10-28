#pragma once
#include "media_library.h"
#include <functional>
#include <memory>

namespace ttpbrowser {
class MediaSettings final {
public:
    MediaSettings(media::Library&, std::function<void()> saved);
    ~MediaSettings();
    HWND Create(HWND parent);
    HRESULT Commit();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
