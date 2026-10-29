#pragma once
#include "browser_bridge.h"
#include "ttplayer/core/json.h"
#include <filesystem>
#include <functional>
#include <mutex>
#include <climits>

namespace ttpbrowser::media {
using Cancel = std::function<bool()>;
using Json = ttplayer::core::Json;
// Empty body selects GET. API requests never share browser cookies.
using Transport = std::function<std::string(const std::wstring&, const std::string&,
                                           const std::wstring&, const Cancel&)>;
struct Error { HRESULT code; std::wstring message; };
enum class Provider { Subsonic, OpenList, WebDav };
enum class Auth { Password, ApiKey, Token, Guest };
struct ResolvedSource { std::wstring url, authorization; };
using DavTransport = std::function<std::string(const std::wstring&,const std::wstring&,const Cancel&)>;
std::string WebDavRequest(const std::wstring&,const std::wstring&,const Cancel&);
struct Profile {
    Provider provider{};
    Auth auth{};
    std::wstring id, name, url, username, secret, root{L"/"}, directory_password;
    // Proxy keeps storage-specific headers and cookies on the OpenList server.
    bool proxy{true};
};
Profile NewProfile(Provider);
enum class Kind { Song, Album, Artist, Playlist, Directory };
struct Item {
    Kind kind{};
    std::wstring id, title, artist, album, suffix, cover;
    uint64_t duration{}, size{};
};
enum class View { Albums, Artists, Playlists, Songs, Starred, Directory, Album, Artist, Playlist, Search };
struct Query { View view{View::Albums}; std::wstring id, text; unsigned page{}; };
struct Page { std::vector<Item> items; bool more{}; std::wstring description; };
std::string Utf8(const std::wstring&);
std::wstring Wide(const std::string&);
std::string Quote(const std::wstring&);
std::wstring Encode(const std::wstring&, bool path = false);
std::wstring Decode(const std::wstring&);
std::wstring NormalizeUrl(std::wstring);
// Repair reversible legacy GBK tags exposed as Latin-1 by media servers.
// Display metadata only: never use this on IDs, paths or credentials.
std::wstring DisplayMetadata(const std::wstring&);
bool IsSource(const std::wstring&);
std::wstring Source(const Profile&, const Item&, bool download = false);
std::string Request(const std::wstring&, const std::string&, const std::wstring&, const Cancel&);

class Library final {
public:
    explicit Library(std::filesystem::path state_file, Transport transport = Request, DavTransport dav = WebDavRequest);
    std::vector<Profile> Profiles() const;
    unsigned ActiveProfile() const;
    void Save(const std::vector<Profile>&, unsigned active = UINT_MAX);
    void SelectProfile(const std::wstring& id);
    Profile Find(const std::wstring&) const;
    void Test(const Profile&, const Cancel& = {});
    Page Browse(const Profile&, const Query&, const Cancel& = {});
    std::vector<Item> Playable(const Profile&, const Item&, const Cancel& = {});
    std::wstring Resolve(const std::wstring&, const Cancel& = {});
    ResolvedSource ResolveRequest(const std::wstring&, const Cancel& = {});
    std::string Cover(const Profile&, const std::wstring&, const Cancel& = {});
    std::wstring Lyrics(const Profile&, const Item&, const Cancel& = {});
    const std::wstring& LoadError() const { return load_error_; }
private:
    Page WebDav(const Profile&, const Query&, const Cancel&);
    Json Subsonic(const Profile&, const wchar_t*, const std::wstring&, const Cancel&);
    std::wstring SubsonicUrl(const Profile&, const wchar_t*, const std::wstring&);
    Json OpenList(const Profile&, const wchar_t*, const std::string&, const Cancel&);
    std::filesystem::path file_;
    Transport transport_;
    DavTransport dav_;
    mutable std::mutex mutex_;
    std::vector<Profile> profiles_;
    unsigned active_profile_{};
    struct Session { std::wstring credentials, token; };
    std::map<std::wstring, Session> sessions_;
    std::wstring load_error_;
};
}
