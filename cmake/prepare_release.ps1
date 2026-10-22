[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$PreviousVersion,
    [Parameter(Mandatory = $true)][string]$Repository,
    [Parameter(Mandatory = $true)][string]$Commit,
    [string]$ArtifactDirectory = 'artifact',
    [string]$ServerUrl = 'https://github.com'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'version.ps1')
$version = (Get-BrowserBuildVersion $Version).Name
if ($PreviousVersion -and -not (Read-BrowserVersionTag $PreviousVersion)) { throw 'Invalid previous release version.' }
if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $Commit -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Invalid repository or commit.'
}
$archiveName = "ttpbrowser-$version.zip"
$archive = Join-Path $ArtifactDirectory $archiveName
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
$expected = (Get-Content -LiteralPath (Join-Path $ArtifactDirectory 'SHA256SUMS.txt') -Encoding UTF8 -Raw).Trim()
if ($expected -cne "$hash  $archiveName") { throw 'Music window package SHA-256 verification failed.' }
& python (Join-Path $PSScriptRoot 'verify_package.py') $archive
if ($LASTEXITCODE -ne 0) { throw 'Runtime package contents failed verification.' }
$repoUrl = "$ServerUrl/$Repository"
$logUrl = if ($PreviousVersion) { "$repoUrl/compare/$PreviousVersion...$version" } else { "$repoUrl/commits/$version" }
$notes = @"
**更新记录**: $logUrl

关闭播放器，将 ZIP 中的 ttpbrowser.dll 放在 TTPlayerRebuild.exe 同一目录，并保留第三方声明。
WebView2 Loader 已静态合并，无需单独分发 WebView2Loader.dll。
这是重建版主程序的可选音乐窗及下载管理组件，不安装到 AddIn，也不替换原版 TTPlayer.exe。
默认优先使用 WebView2，无法使用时回退 IE/MSHTML；WebView2 Runtime 需另行安装。
下载队列、原生皮肤、网页脚本桥和主程序的播放/下载接入由该组件提供。
默认保留 mini.qianqian.com 入口；发行本地组件不表示原在线服务恢复。
ZIP 包含一个 x86 DLL、第三方声明及包内 SHA256SUMS.txt。DLL 构建执行 XP/Win7 静态导入审计，未代替旧系统运行验证。

[源码与安装说明]($repoUrl/tree/$Commit)
[构建流程与依赖]($repoUrl/blob/$Commit/docs/BUILD.md)
"@
$notes | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'release-notes.md') -Encoding UTF8
