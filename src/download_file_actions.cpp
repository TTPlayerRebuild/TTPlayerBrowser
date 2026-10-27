#include "download_file_actions.h"
#include <algorithm>
#include <memory>
#include <string>

namespace ttpbrowser {
namespace {
bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    return !_wcsicmp(left.c_str(), right.c_str());
}
std::filesystem::path AbsoluteFile(const std::filesystem::path& path) {
    const auto text = path.wstring();
    if (text.empty() || text.find(L'\0') != std::wstring::npos ||
        text.find_first_of(L"*?") != std::wstring::npos ||
        text.find(L"://") != std::wstring::npos) return {};
    std::error_code error;
    auto result = std::filesystem::absolute(path, error).lexically_normal();
    return error ? std::filesystem::path{} : result;
}
bool ExistingFile(const std::filesystem::path& path, const DownloadFileDeleteOperations& operations) {
    const DWORD attributes = operations.file_attributes(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
bool ConfirmedAbsent(const std::filesystem::path& path, const DownloadFileDeleteOperations& operations) {
    SetLastError(ERROR_SUCCESS);
    if (operations.file_attributes(path.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}
void AppendUnique(std::vector<std::filesystem::path>& values, std::filesystem::path path) {
    if (std::none_of(values.begin(), values.end(), [&](const auto& existing) {return SamePath(existing, path);}))
        values.push_back(std::move(path));
}
struct BstrDelete { void operator()(wchar_t* value) const noexcept { SysFreeString(value); } };
}

DeletedFilesResult DeleteDownloadedFiles(
    HWND owner, const std::vector<std::filesystem::path>& media_paths,
    const ttp_browser_host& host, const DownloadFileDeleteOperations& operations) {
    DeletedFilesResult result;
    if (!operations.shell_file_operation || !operations.file_attributes || !operations.message_box) {
        result.status = E_INVALIDARG; return result;
    }
    try {
        std::vector<std::filesystem::path> media;
        for (const auto& source : media_paths) {
            auto path = AbsoluteFile(source);
            if (!path.empty() && ExistingFile(path, operations)) AppendUnique(media, std::move(path));
        }
        // Missing/stale records stay in the list: this action did not delete
        // their files. In particular, never send a directory to FO_DELETE.
        if (media.empty()) return result;
        std::vector<std::filesystem::path> lyrics;
        if (host.size >= offsetof(ttp_browser_host, resolve_associated_lyric) + sizeof(host.resolve_associated_lyric) &&
            host.resolve_associated_lyric) {
            for (const auto& source : media) {
                BSTR output{};
                const HRESULT resolved = host.resolve_associated_lyric(host.context, source.c_str(), &output);
                const std::unique_ptr<wchar_t, BstrDelete> holder(output);
                if (resolved != S_OK || !output) continue;
                auto path = AbsoluteFile(std::wstring(output, SysStringLen(output)));
                if (path.empty() || !ExistingFile(path, operations) ||
                    std::any_of(media.begin(), media.end(), [&](const auto& existing) { return SamePath(existing, path); })) continue;
                AppendUnique(lyrics, std::move(path));
            }
        }
        bool include_lyrics{};
        if (!lyrics.empty()) {
            const int answer = operations.message_box(owner,
                L"是否同时删除与这些歌曲关联的歌词文件？", L"删除文件",
                MB_YESNOCANCEL | MB_ICONQUESTION);
            if (answer != IDYES && answer != IDNO) return result;
            include_lyrics = answer == IDYES;
        }
        std::wstring sources;
        for (const auto& path : media) { sources.append(path.native()); sources.push_back(L'\0'); }
        if (include_lyrics) for (const auto& path : lyrics) { sources.append(path.native()); sources.push_back(L'\0'); }
        sources.push_back(L'\0');
        // Reserve the result before the Shell makes any external changes so
        // an allocation failure cannot discard an already-deleted media row.
        auto removed = media;
        SHFILEOPSTRUCTW operation{};
        operation.hwnd = owner;
        operation.wFunc = FO_DELETE;
        operation.pFrom = sources.c_str();
        operation.fFlags = FOF_ALLOWUNDO;
        const int error = operations.shell_file_operation(&operation);
        removed.erase(std::remove_if(removed.begin(), removed.end(),
            [&](const auto& path) { return !ConfirmedAbsent(path, operations); }), removed.end());
        result.removed.swap(removed);
        if (operation.fAnyOperationsAborted) result.status = S_FALSE;
        else if (error) result.status = HRESULT_FROM_WIN32(static_cast<DWORD>(error));
        else if (result.removed.size() != media.size()) result.status = E_FAIL;
        else result.status = S_OK;
    } catch (const std::bad_alloc&) { result.status = E_OUTOFMEMORY; }
    catch (...) { result.status = E_FAIL; }
    return result;
}
}
