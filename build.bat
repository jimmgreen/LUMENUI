@echo off
setlocal
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
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
set "LUMATEXT_ARGS="
if exist "%ROOT%\..\lumatext\CMakeLists.txt" set "LUMATEXT_ARGS=-DLUMATEXT_SOURCE_DIR=%ROOT%\..\lumatext"
cmake -S "%ROOT%" -B "%ROOT%\build" -G Ninja -DCMAKE_BUILD_TYPE=Release %LUMATEXT_ARGS% || exit /b 1
cmake --build "%ROOT%\build" || exit /b 1
