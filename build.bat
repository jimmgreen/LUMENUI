@echo off
setlocal
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
rem Optional first argument selects an isolated build directory; existing caches are never deleted.
set "BUILD_DIR=%ROOT%\build"
if not "%~1"=="" set "BUILD_DIR=%~f1"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS (
    echo error: Visual Studio C++ toolset not found.
    exit /b 1
)
call "%VCVARS%" || exit /b 1
cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DLUMEN_BUILD_EXAMPLES=ON -DLUMEN_BUILD_TESTS=ON -DLUMEN_WITH_LUMATEXT=ON -DLUMEN_REQUIRE_LUMATEXT=ON -DLUMEN_USE_PREBUILT_LUMATEXT=ON "-DLUMATEXT_PREBUILT_DIR=%ROOT%\third_party\lumatext" || exit /b 1
cmake --build "%BUILD_DIR%" || exit /b 1
