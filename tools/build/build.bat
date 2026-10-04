@echo off
rem Configure, build and test CloudScope on Windows.
rem   tools\build\build.bat [config] [extra ctest arguments]
rem   config: Debug (default), RelWithDebInfo or Release
setlocal
call "%~dp0win-env.bat" || exit /b 1
set "CS_CONFIG=%~1"
if "%CS_CONFIG%"=="" set "CS_CONFIG=Debug"
pushd "%~dp0..\.."
cmake --preset windows-msvc || (popd & exit /b 1)
cmake --build --preset windows-msvc --config %CS_CONFIG% || (popd & exit /b 1)
ctest --preset windows-msvc -C %CS_CONFIG% %2 %3 %4 %5 %6 %7 %8 %9
set "CS_RC=%ERRORLEVEL%"
popd
exit /b %CS_RC%
