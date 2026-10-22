// Compile as a separate executable linked with ttplayer_core. Run from an
// isolated copy of its runtime, never from an original TTPlayer installation.
#include "ttplayer/ui/player_window.h"
#include "ttplayer/ui/wtl_dialogs.h"
#include "ttplayer/ui/window_drag.h"
#include "player_window_internal.h"
#include "track_metadata.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace ttplayer::testing {
struct SkinRebindAccess {
    static void Require(bool value, const char* message) {
        if (!value) throw std::runtime_error(message);
    }
    static INT_PTR CALLBACK EmptyDialog(HWND, UINT, WPARAM, LPARAM) { return FALSE; }
    static void CheckBrowserDocking(ui::PlayerWindow& player) {
        using namespace ui::detail;
        const HWND browser = player.MusicBrowserWindow();
        const auto rect = [](HWND window) { RECT r{}; GetWindowRect(window, &r); return r; };
        const auto place = [](HWND window, const RECT& r) {
            SetWindowPos(window, nullptr, r.left, r.top, r.right-r.left, r.bottom-r.top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        };
        struct SavedWindow { HWND window; RECT bounds; bool visible; };
        std::vector<SavedWindow> saved;
        for (const HWND window : player.RegisteredDragWindows())
            saved.push_back({window, rect(window), IsWindowVisible(window)!=FALSE});
        const auto old_option = player.settings_.general.snap_windows;
        const auto old_browser = player.settings_.player.browser_window;
        POINT cursor{}; GetCursorPos(&cursor);
        const auto restore = [&] {
            player.EndSkinMouseCapture();
            SendMessageW(browser, WM_CANCELMODE, 0, 0);
            for (const auto& entry : saved) {
                place(entry.window, entry.bounds);
                ShowWindow(entry.window, entry.visible ? SW_SHOWNOACTIVATE : SW_HIDE);
            }
            player.settings_.general.snap_windows = old_option;
            player.settings_.player.browser_window = old_browser;
            SetCursorPos(cursor.x, cursor.y);
        };
        try {
            const RECT work = ui::DragWorkAreaForRect(rect(browser));
            const LONG x=work.left+80, y=work.top+80;
            const auto hide_targets = [&] {
                for (const auto& entry : saved) if (entry.window!=browser) {
                    ShowWindow(entry.window, SW_HIDE);
                    // Keep hidden geometry out of the main window's attached set.
                    place(entry.window, {work.right+1000,work.bottom+1000,work.right+1300,work.bottom+1200});
                }
            };
            const auto adjust = [&](RECT proposed, uint32_t hit=HTCAPTION) {
                Require(SUCCEEDED(player.BrowserAdjustWindowRectCallback(&player,browser,hit,&proposed)),
                    "browser snap callback rejected a valid rectangle");
                return proposed;
            };
            hide_targets();
            player.settings_.general.snap_windows=0x1000a;
            for (int side=0;side<4;++side) for (const LONG gap : {10L,11L}) {
                RECT proposed{x+100,y+100,x+420,y+340};
                if(side==0)OffsetRect(&proposed,work.left+gap-proposed.left,0);
                if(side==1)OffsetRect(&proposed,work.right-gap-proposed.right,0);
                if(side==2)OffsetRect(&proposed,0,work.top+gap-proposed.top);
                if(side==3)OffsetRect(&proposed,0,work.bottom-gap-proposed.bottom);
                RECT expected=proposed;
                if(gap==10)OffsetRect(&expected,side==0?-gap:side==1?gap:0,side==2?-gap:side==3?gap:0);
                const RECT actual=adjust(proposed);
                Require(EqualRect(&actual,&expected),"screen snap must include 10 px and exclude 11 px on all four edges");
            }
            RECT proposed{work.left+22,y+100,work.left+342,y+340};
            player.settings_.general.snap_windows=0x10016;
            Require(adjust(proposed).left==work.left,"browser ignores live custom snap distance");
            player.settings_.general.snap_windows=22;
            Require(adjust(proposed).left==proposed.left,"disabled snap still changes browser geometry");
            player.settings_.general.snap_windows=0x1000a;
            for (const HWND target : {player.window_,player.playlist_window_,player.lyric_window_,player.equalizer_window_}) {
                Require(target && IsWindow(target),"missing registered snap target fixture");
                hide_targets();place(target,{x,y,x+300,y+120});ShowWindow(target,SW_SHOWNOACTIVATE);
                proposed={x+310,y+20,x+630,y+260};
                Require(adjust(proposed).left==x+300,"browser does not snap to a visible registered panel");
                ShowWindow(target,SW_HIDE);
                Require(adjust(proposed).left==x+310,"browser snaps to an invisible panel");
            }
            hide_targets();
            for (uint32_t hit=HTLEFT;hit<=HTBOTTOMRIGHT;++hit) {
                const bool left=hit==HTLEFT||hit==HTTOPLEFT||hit==HTBOTTOMLEFT;
                const bool right=hit==HTRIGHT||hit==HTTOPRIGHT||hit==HTBOTTOMRIGHT;
                const bool top=hit==HTTOP||hit==HTTOPLEFT||hit==HTTOPRIGHT;
                const bool bottom=hit==HTBOTTOM||hit==HTBOTTOMLEFT||hit==HTBOTTOMRIGHT;
                proposed={work.left+10,work.top+10,work.right-10,work.bottom-10};
                RECT expected=proposed;
                if(left)expected.left=work.left;if(right)expected.right=work.right;
                if(top)expected.top=work.top;if(bottom)expected.bottom=work.bottom;
                const RECT actual=adjust(proposed,hit);
                Require(EqualRect(&actual,&expected),"resize snap moved an inactive edge or missed an active edge");
            }
            // Exercise the DLL's actual client-capture path, not only host geometry.
            const LONG width=std::max(320L,player.skin_->Browser().background.size.cx)+80;
            const LONG height=std::max(240L,player.skin_->Browser().background.size.cy)+60;
            const auto drag = [&](POINT point, POINT delta, UINT ending=WM_LBUTTONUP) {
                POINT start=point;ClientToScreen(browser,&start);
                Require(SetCursorPos(start.x,start.y)!=FALSE,"cursor unavailable for browser drag fixture");
                SendMessageW(browser,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));
                Require(GetCapture()==browser,"music window did not capture client drag");
                Require(SetCursorPos(start.x+delta.x,start.y+delta.y)!=FALSE,"cannot move browser test cursor");
                SendMessageW(browser,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(point.x+delta.x,point.y+delta.y));
                SendMessageW(browser,ending,0,0);
                Require(GetCapture()!=browser,"browser drag failed to release capture");
                return rect(browser);
            };
            place(player.window_,{x,y,x+300,y+120});ShowWindow(player.window_,SW_SHOWNOACTIVATE);
            const RECT main_before=rect(player.window_);
            const RECT before{x+350,y+20,x+350+width,y+20+height};place(browser,before);
            RECT actual=drag({width*2/3,12},{-40,0});
            Require(actual.left==x+300 && actual.top==before.top,"DLL client drag failed to apply host snap");
            const RECT main_after=rect(player.window_);
            Require(EqualRect(&main_before,&main_after),"dragging browser incorrectly moved the main window");
            Require(EqualRect(&actual,&player.settings_.player.browser_window),"snapped browser bounds were not persisted in host state");
            place(browser,before);
            actual=drag({width*2/3,12},{-40,0},WM_CANCELMODE);
            Require(EqualRect(&actual,&before),"canceling snapped drag did not restore starting bounds");
            player.settings_.general.snap_windows=10;place(browser,before);
            Require(drag({width*2/3,12},{-40,0}).left==x+310,"DLL did not observe disabled snap setting");
            player.settings_.general.snap_windows=0x1000a;
            place(player.window_,{x+width+80,y,x+width+380,y+120});
            place(browser,{x,y+20,x+width,y+20+height});
            actual=drag({width-2,height/2},{70,0});
            Require(actual.right==x+width+80 && actual.left==x,"DLL right resize failed to snap with opposite edge fixed");
            // A magnetic target must not force the skin below its native width.
            place(player.window_,{x+88,y,x+388,y+120});
            place(browser,{x,y+20,x+width,y+20+height});
            actual=drag({2,height/2},{82,0});
            Require(actual.left==x+80 && actual.right==x+width,"magnetic resize bypassed the skin minimum width");
            // Main -> playlist -> browser is a transitive attached group.
            hide_targets();
            place(player.window_,{x,y,x+300,y+120});ShowWindow(player.window_,SW_SHOWNOACTIVATE);
            place(player.playlist_window_,{x+300,y,x+600,y+120});ShowWindow(player.playlist_window_,SW_SHOWNOACTIVATE);
            place(browser,{x+600,y,x+600+width,y+height});
            player.BeginSkinBackgroundDrag(player.window_,{80,20},kDragMove);
            Require(std::find(player.attached_drag_windows_.begin(),player.attached_drag_windows_.end(),browser)
                !=player.attached_drag_windows_.end(),"main drag omitted the transitively attached music window");
            player.ContinueSkinBackgroundDrag(player.window_,{110,45});player.EndSkinMouseCapture();
            actual=rect(browser);
            Require(actual.left==x+630 && actual.top==y+25,"main drag did not move attached browser by the same offset");
            player.CaptureMusicBrowserState();
            Require(EqualRect(&actual,&player.settings_.player.browser_window),"group-moved browser state did not refresh");
            std::cout << "browser docking: screen 10/11px, custom/disabled, four panels, hidden targets, eight resize grips, "
                "DLL move/resize, cancel, minimum size, independent drag, transitive group, state PASS\n";
        } catch (...) { restore();throw; }
        restore();
    }
    static void CaptureWindow(HWND window, const std::filesystem::path& path, int margin = 0) {
        RECT bounds{}; Require(GetWindowRect(window, &bounds) != FALSE, "screenshot geometry unavailable");
        InflateRect(&bounds, margin, margin);
        // Include the surrounding area against a controlled backdrop: cropping
        // to the HWND rectangle would hide an erroneous compositor frame.
        struct Backdrop {
            HWND hwnd{};
            ~Backdrop() { if (hwnd) DestroyWindow(hwnd); }
        } backdrop;
        if (margin) {
            WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW;
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH));
            cls.lpszClassName = L"TTPlayer.HostProbeBackdrop";
            Require(RegisterClassW(&cls) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
                    "screenshot backdrop registration failed");
            backdrop.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, L"",
                WS_POPUP | WS_VISIBLE, bounds.left, bounds.top,
                bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr,
                GetModuleHandleW(nullptr), nullptr);
            Require(backdrop.hwnd != nullptr, "screenshot backdrop unavailable");
            SetWindowPos(backdrop.hwnd, window, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            UpdateWindow(backdrop.hwnd);
        }
        SetForegroundWindow(window);
        RedrawWindow(window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
        // Let native child paints and the compositor publish the new size.
        const auto deadline = GetTickCount64() + 150;
        do {
            MSG queued{};
            while (PeekMessageW(&queued, nullptr, 0, 0, PM_REMOVE)) {
                if (queued.message == WM_QUIT) continue;
                TranslateMessage(&queued); DispatchMessageW(&queued);
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, 15, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        } while (GetTickCount64() < deadline);
        const LONG width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        Require(width > 0 && height > 0 && width < 16384 && height < 16384, "invalid screenshot dimensions");
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        info.bmiHeader.biSizeImage = static_cast<DWORD>(width * height * 4);
        HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
        void* pixels{};
        HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!memory || !bitmap) {
            if (memory) DeleteDC(memory); if (bitmap) DeleteObject(bitmap); ReleaseDC(nullptr, screen);
            throw std::runtime_error("screenshot bitmap allocation failed");
        }
        const auto previous = SelectObject(memory, bitmap);
        const bool captured = BitBlt(memory, 0, 0, width, height, screen, bounds.left, bounds.top,
                                    SRCCOPY | CAPTUREBLT) != FALSE;
        GdiFlush();
        BITMAPFILEHEADER file{}; file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
        file.bfSize = file.bfOffBits + info.bmiHeader.biSizeImage;
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&file), sizeof(file));
        output.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
        if (captured) output.write(static_cast<const char*>(pixels), info.bmiHeader.biSizeImage);
        output.close();
        const bool saved = static_cast<bool>(output);
        SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
        Require(captured && saved, "window screenshot failed");
        std::wcout << L"screenshot " << path.filename().wstring() << L" " << width << L"x" << height << L'\n';
    }
    static void Run(const std::filesystem::path& runtime, HMODULE resources, HMODULE comm,
                    bool present, const std::wstring& home) {
        using namespace ui::detail;
        settings::Settings settings;
        settings.source_path = runtime / L"HostProbe.xml";
        settings.skin_file = L"<Default_Skin>";
        settings.general.fade_windows = false;
        settings.general.tray_icon = false;
        settings.general.send_title_to_msn = false;
        settings.player.mute = true;
        settings.player.volume = 0;
        settings.player.browser_auto_startup = false;
        settings.player.browser_visible = false;
        settings.player.browser_home_url = home;
        settings.network.cache_folder = runtime / L"Cache";
        settings.network.download_folder = runtime / L"Downloads";
        settings.lyric.auto_download = false;
        settings.playlist.read_info_mode = 2;
        ui::PlayerWindow player(settings);
        player.SetSkinResourceModule(resources);
        player.SetTtpCommModule(comm);
        Require(player.BrowserAvailable() == present, "module availability disagrees with isolated fixture");
        Require(player.LoadSkinResource(resources), "cannot load default skin");
        Require(player.Create(GetModuleHandleW(nullptr), SW_HIDE), "cannot create host window");
        player.CompleteSkinWindowFadeForReplacement();
        try {
            const auto main = player.BuildContextMenu();
            Require(main != nullptr, "main menu unavailable");
            const bool main_entry = FindCommandMenu(main, kCmdShowBrowser) != nullptr;
            DestroyMenu(main);
            Require(main_entry == present, "main music command gating failed");
            Require(player.IsSkinElementEnabled(L"browser") == present, "main skin button gating failed");
            if (!present) {
                player.UpdateMainToolRects();
                Require(std::none_of(player.tooltip_tools_.begin(), player.tooltip_tools_.end(),
                    [&](const auto& tool) { return tool.owner == player.window_ && tool.identifier == kCmdShowBrowser; }),
                    "absent module left a music-window tooltip registered");
                if (const auto* control = player.skin_->Find(L"browser")) {
                    const POINT point{control->bounds.left + 1, control->bounds.top + 1};
                    Require(player.HitTestSkin(point) != L"browser", "hidden skin control remains a mouse target");
                }
            }
            const HMENU playlist = CreatePopupMenu();
            AppendMenuW(playlist, MF_STRING, kPlaylistDownload, L"Download");
            player.PreparePlaylistMenu(playlist);
            const bool download_entry = FindCommandMenu(playlist, kPlaylistDownload) != nullptr;
            DestroyMenu(playlist);
            Require(download_entry == present, "playlist download entry gating failed");

            HWND shortcuts = ui::CreateWtlDialog(resources, MAKEINTRESOURCEW(252), player.window_, EmptyDialog, 0);
            Require(shortcuts != nullptr, "cannot create shortcut options page");
            player.InitializeOptionsPage(shortcuts, 252);
            bool shortcut_entry{};
            for (auto row : player.options_hotkey_rows_)
                shortcut_entry |= player.settings_.hotkey.key_map[row].command == kCmdShowBrowser;
            Require(shortcut_entry == present, "shortcut options row gating failed");
            DestroyWindow(shortcuts);

            HWND network = ui::CreateWtlDialog(resources, MAKEINTRESOURCEW(258), player.window_, EmptyDialog, 0);
            Require(network != nullptr, "cannot create network options page");
            player.InitializeOptionsPage(network, 258);
            Require(TabCtrl_GetItemCount(GetDlgItem(network, 1181)) == (present ? 2 : 1),
                    "network download tab gating failed");
            Require((player.options_network_pages_[1] != nullptr) == present,
                    "hidden download settings page was created");
            DestroyWindow(network);
            player.options_network_pages_ = {};
            player.options_network_child_ = nullptr;

            MSG key{}; key.hwnd = player.window_; key.message = WM_KEYDOWN; key.wParam = VK_F11;
            if (!present) {
                for (auto& binding : player.settings_.hotkey.key_map) {
                    binding.global = {};
                    if (binding.command == kCmdShowBrowser) {
                        binding.global.virtual_key = VK_F11;
                        binding.global.modifiers = HOTKEYF_CONTROL | HOTKEYF_ALT;
                    }
                }
                player.settings_.hotkey.global = true;
                player.RegisterConfiguredHotKeys(false);
                Require(player.registered_hotkey_ids_.empty(), "absent module registered a global music shortcut");
                Require(!player.PreTranslateMessage(key), "absent module still consumes F11");
                Require(player.HandleContextCommand(kCmdShowBrowser), "stale browser command not handled");
                player.ShowNetworkDownloads();
                Require(!player.MusicBrowserWindow(), "absent module created a browser window");
                Require(GetModuleHandleW(L"ttpbrowser.dll") == nullptr, "absent module was searched outside executable directory");
            } else {
                player.ShowNetworkDownloads();
                // Keep the surrounding screenshot area free of the fixture's
                // other skin windows, whose defaults are enabled by LoadSkin.
                player.settings_.player.playlist_visible = false;
                player.settings_.player.lyric_visible = false;
                if (player.playlist_window_) ShowWindow(player.playlist_window_, SW_HIDE);
                if (player.lyric_window_) ShowWindow(player.lyric_window_, SW_HIDE);
                Require(player.MusicBrowserWindow() && IsWindowVisible(player.MusicBrowserWindow()),
                        "present module did not create downloads window");
                CheckBrowserDocking(player);
                SetWindowPos(player.MusicBrowserWindow(), HWND_TOP, 600, 200, 0, 0,
                             SWP_NOSIZE | SWP_NOACTIVATE);
                CaptureWindow(player.MusicBrowserWindow(), runtime / L"browser.bmp", 12);
                RECT bounds{}; GetWindowRect(player.MusicBrowserWindow(), &bounds);
                SetWindowPos(player.MusicBrowserWindow(), nullptr, 0, 0,
                    bounds.right - bounds.left + 120, bounds.bottom - bounds.top + 80,
                    SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
                CaptureWindow(player.MusicBrowserWindow(), runtime / L"browser-resized.bmp", 12);
                Require(player.PreTranslateMessage(key), "present module did not handle F11");
                Require(!IsWindowVisible(player.MusicBrowserWindow()), "F11 failed to toggle browser visibility");
                BSTR version{};
                Require(SUCCEEDED(player.QueryMusicBrowserInfo(1, 1, &version)) &&
                        version && std::wstring(version) == L"5.7.9", "legacy version callback mismatch");
                SysFreeString(version);
                const ttp_browser_track track{L"http://127.0.0.1:9/probe.mp3", L"Probe Artist", L"Probe Title", L"probe-tid"};
                const auto old_count = player.ActivePlaylist().Tracks().size();
                Require(SUCCEEDED(player.SubmitMusicBrowserTracks(&track, 1, 0, 1, 0)), "script track append failed");
                Require(player.ActivePlaylist().Tracks().size() == old_count + 1, "script import count mismatch");
                const auto& imported = player.ActivePlaylist().Tracks().back();
                Require(imported.artist == "Probe Artist" && imported.title == "Probe Title" &&
                    std::find(imported.metadata.begin(), imported.metadata.end(),
                        std::pair<std::string,std::string>{"TTPlayer.Tid","probe-tid"}) != imported.metadata.end(),
                    "script metadata was lost");
                std::vector<std::pair<std::string,std::string>> replacement{{"Title","Updated"}};
                PreserveBrowserTrackIdentity(imported.metadata, replacement);
                Require(replacement.back() == std::pair<std::string,std::string>{"TTPlayer.Tid","probe-tid"},
                    "reader metadata refresh loses service identity");
            }
            player.settings_.player.browser_window = {100, 110, 580, 596};
            player.settings_.player.browser_window2 = {120, 130, 600, 616};
            player.settings_.player.browser_auto_startup = true;
            player.settings_.player.browser_visible = true;
            Require(static_cast<bool>(settings::SaveWindowState(settings.source_path, player.settings_)), "window-state save failed");
            const auto restored = settings::LoadLegacyXml(settings.source_path);
            Require(EqualRect(&restored.player.browser_window, &player.settings_.player.browser_window) &&
                EqualRect(&restored.player.browser_window2, &player.settings_.player.browser_window2) &&
                restored.player.browser_auto_startup && restored.player.browser_visible && restored.player.browser_home_url == home,
                "music window settings failed to round-trip");
            std::cout << "host module=" << (present ? "present" : "absent")
                      << " main-menu, skin-hit, F11, playlist-download, shortcut-page, network-page, settings PASS\n";
        } catch (...) {
            if (IsWindow(player.window_)) DestroyWindow(player.window_);
            throw;
        }
        DestroyWindow(player.window_);
    }
};
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 3 || std::wstring(argv[1]) != L"--isolated")
            throw std::runtime_error("run only in a copied runtime: --isolated --expect-absent|--expect-present [loopback home]");
        const bool present = std::wstring(argv[2]) == L"--expect-present";
        if (!present && std::wstring(argv[2]) != L"--expect-absent") throw std::runtime_error("unknown fixture mode");
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        const auto runtime = std::filesystem::path(executable).parent_path();
        if (!std::filesystem::exists(runtime / L".host-probe-isolated"))
            throw std::runtime_error("missing isolation marker .host-probe-isolated");
        const HRESULT ole = OleInitialize(nullptr);
        if (FAILED(ole)) throw std::runtime_error("OLE initialization failed");
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&common);
        HMODULE resources = LoadLibraryExW((runtime / L"ttpres.dll").c_str(), nullptr,
            LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        HMODULE comm = LoadLibraryW((runtime / L"ttpcomm.dll").c_str());
        if (!resources || !comm) throw std::runtime_error("fixture resource/inflater DLL missing");
        ttplayer::testing::SkinRebindAccess::Run(runtime, resources, comm, present,
            argc > 3 ? argv[3] : L"http://127.0.0.1:9/index.html");
        FreeLibrary(comm); FreeLibrary(resources); OleUninitialize();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "host probe: " << error.what() << '\n'; return 1;
    }
}
