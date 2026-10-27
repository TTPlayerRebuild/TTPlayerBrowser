#pragma once
#include "ttpbrowser.h"
#include <shellapi.h>
#include <filesystem>
#include <vector>

namespace ttpbrowser {
struct DeletedFilesResult {
    HRESULT status{S_FALSE};
    // Only media files that existed before the operation and were confirmed
    // absent afterwards. This may be nonempty after a partial failure/cancel.
    std::vector<std::filesystem::path> removed;
};

// Native defaults; the narrow injectable boundary allows cancellation and
// partial Shell failures to be exercised without deleting any real files.
struct DownloadFileDeleteOperations {
    int (WINAPI* shell_file_operation)(LPSHFILEOPSTRUCTW){SHFileOperationW};
    DWORD (WINAPI* file_attributes)(LPCWSTR){GetFileAttributesW};
    int (WINAPI* message_box)(HWND, LPCWSTR, LPCWSTR, UINT){MessageBoxW};
};
DeletedFilesResult DeleteDownloadedFiles(
    HWND owner, const std::vector<std::filesystem::path>& media_paths,
    const ttp_browser_host& host,
    const DownloadFileDeleteOperations& operations = {});
}
