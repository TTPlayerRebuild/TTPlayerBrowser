#pragma once
#include "ttpbrowser.h"
#include "browser_bridge.h"
#include "webview_backend.h"
#include <wrl/client.h>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace ttpbrowser {
class Downloads;
class BrowserWindow final {
public:
    BrowserWindow(const ttp_browser_host&, const ttp_browser_config&, Downloads&);
    ~BrowserWindow();
    HRESULT Configure(const ttp_browser_config&);
    HRESULT SetSkin(const ttp_browser_skin&);
    HRESULT Show(uint32_t);
    HWND Window() const { return window_; }
    BOOL Translate(MSG*);
    HRESULT GetState(ttp_browser_state*) const;
    HRESULT ExternalDispatch(IDispatch**);
private:
    struct Action {
        enum Kind { Submit, Download, Setup, Hyperlink } kind{};
        std::vector<BrowserTrack> tracks;
        int modes[3]{};
        std::wstring a, b, c;
        uint64_t document_generation{};
    };
    ttp_browser_host host_{};
    Downloads& downloads_;
    HWND window_{}, tooltip_{}, error_panel_{}, error_text_{}, retry_button_{};
    HINSTANCE instance_{};
    std::wstring state_file_, home_url_, origin_, profile_, current_url_, backend_name_, status_, link_text_, link_url_;
    bool autostart_{}, downloads_view_{}, can_back_{}, can_forward_{}, browser_ready_{}, modern_attempt_{}, error_visible_{}, destroying_{}, loading_{}, first_position_{true};
    bool topmost_{};
    BYTE opacity_{255};
    uint32_t engine_{};
    int hover_{-1}, pressed_{-1};
    int drag_hit_{HTNOWHERE};
    POINT drag_anchor_{};
    RECT drag_bounds_{};
    SIZE region_size_{};
    bool region_dirty_{true}, updating_region_{};
    ttp_browser_state state_{};
    ttp_browser_skin skin_{};
    std::shared_ptr<BrowserBackend> backend_;
    std::shared_ptr<BridgeContext> bridge_;
    Microsoft::WRL::ComPtr<IDispatch> external_;
    std::deque<Action> actions_;
    HRESULT EnsureWindow();
    HRESULT StartBrowser(bool force_ie = false);
    void Layout();
    void Paint();
    void Activate(int);
    void SwitchView(bool);
    void StateChanged();
    void ProcessActions();
    HRESULT Queue(Action);
    void NavigationStarting(const std::wstring&);
    void NavigationCompleted(bool, const std::wstring&);
    void Error(const std::wstring&);
    void Retry();
    void FreeSkin();
    void UpdateTooltips();
    void UpdateWindowRegion();
    void ApplyFramePolicy();
    int BackgroundHit(POINT) const;
    void BeginBackgroundDrag(POINT);
    void ContinueBackgroundDrag();
    void EndBackgroundDrag(bool cancel = false);
    SIZE MinimumSize() const;
    void Recommendations();
    RECT ContentRect() const;
    RECT ControlRect(int) const;
    int Hit(POINT) const;
    bool Enabled(int) const;
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
};
}
