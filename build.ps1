[CmdletBinding()]
param(
    [string]$BuildDirectory,
    [string]$Generator = 'Visual Studio 18 2026',
    [string[]]$CMakeArguments = @(),
    [switch]$Package,
    [string]$PackageVersion
)
$ErrorActionPreference = 'Stop'
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $PSScriptRoot 'build' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
. (Join-Path $PSScriptRoot 'cmake/version.ps1')
$PackageVersion = (Get-BrowserBuildVersion $PackageVersion).Name
function Invoke-CMake([string[]]$Arguments) {
    & cmake @Arguments
    if ($LASTEXITCODE -ne 0) { throw "CMake failed ($LASTEXITCODE): $Arguments" }
}
Invoke-CMake (@('-S',$PSScriptRoot,'-B',$BuildDirectory,'-G',$Generator,'-A','Win32') + $CMakeArguments +
    @("-DTTPBROWSER_BUILD_VERSION=$PackageVersion",'-DTTPBROWSER_BUILD_TESTS=OFF'))
Invoke-CMake @('--build',$BuildDirectory,'--config','Release','--target','ttpbrowser','--parallel','4')
Assert-BrowserFileVersion (Join-Path $BuildDirectory 'Release/ttpbrowser.dll') $PackageVersion
if (-not $Package) { return }
$output = Join-Path $BuildDirectory 'Release'
$stage = Join-Path $BuildDirectory ('package-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
Invoke-CMake @('--install',$BuildDirectory,'--config','Release','--component','Runtime','--prefix',$stage)
# The loader is redistributed with its notices. Bundle the fixed dependency
# notices as distribution materials, separate from source and test evidence.
$notices = "# Third-party notices`n`n"
foreach ($name in @('WebView2-LICENSE.md','WebView2-NOTICE.md','WTL-MS-PL.md','VC-LTL-LICENSE.md','YY-Thunks-LICENSE.md')) {
    $notices += "## $name`n`n" + [IO.File]::ReadAllText((Join-Path $PSScriptRoot "docs/licenses/$name")) + "`n`n"
}
[IO.File]::WriteAllText((Join-Path $stage 'THIRD_PARTY_NOTICES.md'),$notices,(New-Object Text.UTF8Encoding($false)))
$members = @('ttpbrowser.dll','THIRD_PARTY_NOTICES.md')
$checksums = foreach ($name in $members) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $stage $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name"
}
$checksums | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding UTF8
$archive = Join-Path $output "ttpbrowser-$PackageVersion.zip"
$paths = @($members + 'SHA256SUMS.txt' | ForEach-Object { Join-Path $stage $_ })
Compress-Archive -LiteralPath $paths -DestinationPath $archive -CompressionLevel Optimal -Force
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$archiveHash  ttpbrowser-$PackageVersion.zip" | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding UTF8
& python (Join-Path $PSScriptRoot 'cmake/verify_package.py') $archive
if ($LASTEXITCODE -ne 0) { throw 'Runtime package verification failed.' }
Write-Output "Music window package: $archive"
