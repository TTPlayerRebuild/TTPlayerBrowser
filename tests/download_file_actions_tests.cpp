#include "download_file_actions.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {
int failures{};
void Check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
const std::filesystem::path first = LR"(C:\TTPBrowser.MockOnly\first.mp3)";
const std::filesystem::path second = LR"(C:\TTPBrowser.MockOnly\second.mp3)";
const std::filesystem::path lyric = LR"(C:\TTPBrowser.MockOnly\explicit-association.lrc)";
const std::filesystem::path missing = LR"(C:\TTPBrowser.MockOnly\missing.mp3)";
const std::filesystem::path directory = LR"(C:\TTPBrowser.MockOnly\folder)";
struct Scenario {
    std::map<std::wstring, DWORD> files;
    std::vector<std::wstring> requested;
    std::vector<std::wstring> delete_paths;
    std::wstring inaccessible_after;
    bool abort{};
    int error{}, answer{IDYES}, shell_calls{}, prompts{}, resolves{};
    bool operation_contract{};
    bool association{};
} state;
void Reset() {
    state = {};
    state.answer = IDYES;
    for (const auto& path : {first, second, lyric}) state.files[path.native()] = FILE_ATTRIBUTE_NORMAL;
    state.files[directory.native()] = FILE_ATTRIBUTE_DIRECTORY;
}
DWORD WINAPI Attributes(LPCWSTR path) {
    if (state.shell_calls && state.inaccessible_after == path) {
        SetLastError(ERROR_ACCESS_DENIED); return INVALID_FILE_ATTRIBUTES;
    }
    const auto found = state.files.find(path);
    if (found == state.files.end()) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_FILE_ATTRIBUTES; }
    return found->second;
}
int WINAPI ShellDelete(LPSHFILEOPSTRUCTW operation) {
    ++state.shell_calls;
    const wchar_t* cursor = operation->pFrom;
    for (; cursor && *cursor; cursor += wcslen(cursor) + 1) state.requested.emplace_back(cursor);
    state.operation_contract = operation->wFunc == FO_DELETE && operation->fFlags == FOF_ALLOWUNDO &&
        cursor && !*cursor && cursor != operation->pFrom && cursor[-1] == L'\0';
    for (const auto& path : state.delete_paths) state.files.erase(path);
    operation->fAnyOperationsAborted = state.abort;
    return state.error;
}
int WINAPI Prompt(HWND, LPCWSTR, LPCWSTR, UINT flags) {
    ++state.prompts;
    Check((flags & MB_TYPEMASK) == MB_YESNOCANCEL, "associated lyric choice includes cancel");
    return state.answer;
}
HRESULT WINAPI Resolve(void*, const wchar_t*, BSTR* out) {
    ++state.resolves;
    *out = state.association ? SysAllocString(lyric.c_str()) : nullptr;
    return *out ? S_OK : S_FALSE;
}
const ttpbrowser::DownloadFileDeleteOperations operations{ShellDelete, Attributes, Prompt};
ttp_browser_host Host() {
    ttp_browser_host host{};
    host.size = sizeof(host); host.version = TTP_BROWSER_ABI_VERSION;
    host.resolve_associated_lyric = Resolve;
    return host;
}
}
int main() {
    auto host = Host();
    Reset();
    state.delete_paths = {first.native(), second.native()};
    auto result = ttpbrowser::DeleteDownloadedFiles(nullptr, {first, second, first}, host, operations);
    Check(result.status == S_OK && result.removed == std::vector<std::filesystem::path>{first, second},
        "successful recycle removes only unique selected media records");
    Check(state.operation_contract && state.requested == state.delete_paths && !state.prompts,
        "Shell receives absolute double-NUL paths and ALLOWUNDO without bypassing its confirmation");

    Reset(); state.association = true;
    state.delete_paths = {first.native(), lyric.native()};
    result = ttpbrowser::DeleteDownloadedFiles(nullptr, {first}, host, operations);
    Check(result.status == S_OK && result.removed == std::vector<std::filesystem::path>{first} &&
        state.requested == state.delete_paths && state.prompts == 1,
        "opted-in exact host lyric association joins Shell request but is not a media record");

    Reset(); state.association = true; state.answer = IDNO; state.delete_paths = {first.native()};
    result = ttpbrowser::DeleteDownloadedFiles(nullptr, {first}, host, operations);
    Check(result.status == S_OK && state.requested == state.delete_paths && state.files.contains(lyric.native()),
        "declining lyric deletion still deletes media and preserves the associated lyric");

    Reset(); state.association = true; state.answer = IDCANCEL;
    result = ttpbrowser::DeleteDownloadedFiles(nullptr, {first}, host, operations);
    Check(result.status == S_FALSE && result.removed.empty() && !state.shell_calls,
        "canceling lyric choice leaves all files and queue records unchanged");

    Reset(); state.abort = true;
    result = ttpbrowser::DeleteDownloadedFiles(nullptr, {first, second}, host, operations);
    Check(result.status == S_FALSE && result.removed.empty(),
        "Shell cancellation before deletion preserves all records");

    Reset(); state.abort = true; state.delete_paths = {first.native()};
    result = ttpbrowser::DeleteDownloadedFiles(nullptr, {first, second}, host, operations);
    Check(result.status == S_FALSE && result.removed == std::vector<std::filesystem::path>{first},
        "partial Shell cancellation removes only already-deleted media records");

    Reset(); state.error = ERROR_ACCESS_DENIED; state.delete_paths = {first.native()};
    state.inaccessible_after = second.native();
    result = ttpbrowser::DeleteDownloadedFiles(nullptr, {first, second}, host, operations);
    Check(FAILED(result.status) && result.removed == std::vector<std::filesystem::path>{first},
        "partial failure retains inaccessible media because missing attributes do not prove absence");

    Reset();
    result = ttpbrowser::DeleteDownloadedFiles(nullptr, {missing, directory}, host, operations);
    Check(result.status == S_FALSE && result.removed.empty() && !state.shell_calls,
        "preexisting missing files and directories cannot delete files or remove records");

    Reset(); state.association = true; state.delete_paths = {first.native()};
    host.size = TTP_BROWSER_HOST_V1_SIZE;
    result = ttpbrowser::DeleteDownloadedFiles(nullptr, {first}, host, operations);
    Check(result.status == S_OK && !state.resolves && !state.prompts && state.requested == state.delete_paths,
        "old hosts cannot supply an association and no same-name lyric is guessed");
    return failures ? 1 : 0;
}
