@echo off
rem Sets up the MSVC x64 build environment for CloudScope in the current command prompt.
rem   - locates Visual Studio (or Build Tools) with vswhere and calls VsDevCmd
rem   - keeps VCPKG_ROOT if you set one, otherwise uses the vcpkg that ships with Visual Studio
rem   - finds Qt: uses QT_ROOT_DIR if set, otherwise the newest C:\Qt\6.x\msvc2022_64

set "CS_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%CS_VSWHERE%" (
  echo [win-env] vswhere.exe not found. Install Visual Studio 2022 Build Tools with the C++ workload. 1>&2
  exit /b 1
)
set "CS_VSDIR="
for /f "usebackq delims=" %%i in (`"%CS_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CS_VSDIR=%%i"
if not defined CS_VSDIR (
  echo [win-env] No Visual Studio installation with the MSVC x64 toolset was found. 1>&2
  exit /b 1
)

set "CS_USER_VCPKG_ROOT=%VCPKG_ROOT%"
call "%CS_VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 -no_logo
if errorlevel 1 exit /b 1
if defined CS_USER_VCPKG_ROOT set "VCPKG_ROOT=%CS_USER_VCPKG_ROOT%"
if not defined VCPKG_ROOT (
  echo [win-env] VCPKG_ROOT is not set and Visual Studio has no vcpkg component. 1>&2
  echo [win-env] Install the "vcpkg package manager" component or clone https://github.com/microsoft/vcpkg and set VCPKG_ROOT. 1>&2
  exit /b 1
)

if not defined QT_ROOT_DIR (
  for /f "delims=" %%d in ('dir /b /ad /on "C:\Qt\6.*" 2^>nul') do (
    if exist "C:\Qt\%%d\msvc2022_64\bin\Qt6Core.dll" set "QT_ROOT_DIR=C:\Qt\%%d\msvc2022_64"
  )
)
if not defined QT_ROOT_DIR (
  echo [win-env] Qt 6 for MSVC 2022 x64 not found. Set QT_ROOT_DIR, e.g. C:\Qt\6.8.3\msvc2022_64 1>&2
  exit /b 1
)
set "CS_VSWHERE="
set "CS_VSDIR="
set "CS_USER_VCPKG_ROOT="
exit /b 0
