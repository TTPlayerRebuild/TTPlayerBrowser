@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if errorlevel 1 exit /b %errorlevel%
if not exist "%~dp0out" mkdir "%~dp0out"
pushd "%~dp0out"
cl /nologo /std:c++20 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /MD /I"%~dp0..\include" /I"%~dp0..\src" /I"%~dp0..\..\rebuild\include" /I"%~dp0..\..\rebuild\third_party\ttpcomm-sdk\include" "%~dp0downloads_tests.cpp" "%~dp0..\src\downloads.cpp" "%~dp0..\src\download_skin.cpp" "%~dp0..\src\http_download_queue.cpp" "%~dp0..\..\rebuild\src\skin\skin_package.cpp" "%~dp0..\..\rebuild\src\audio\legacy_cache.cpp" "%~dp0..\..\rebuild\src\core\text.cpp" /Fe:downloads_tests.exe /link ole32.lib oleaut32.lib comsuppw.lib advapi32.lib comctl32.lib comdlg32.lib user32.lib gdi32.lib shell32.lib uuid.lib msimg32.lib
set result=%errorlevel%
if "%result%"=="0" downloads_tests.exe
if "%result%"=="0" set result=%errorlevel%
popd
exit /b %result%
