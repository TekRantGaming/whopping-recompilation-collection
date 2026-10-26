@echo off
rem TEMPLATE copied from King Kong Recompiled 1.9.x (kk\build.bat). RENAME the preset names (kk-*) and kk_build_root.
rem It puts VS LLVM clang first on PATH (the retcomm clang trap) and builds through a junction when the path has an apostrophe.
rem Usage: build.bat [kk-debug|kk-release|bb-relwithdebinfo|kk-dev]
setlocal EnableDelayedExpansion
set PRESET=%1
if "%PRESET%"=="" set PRESET=bb-relwithdebinfo
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=%VSPATH%\VC\Tools\Llvm\x64\bin;%PATH%

rem The linker's response file treats ' as a quote, so a path like
rem "Peter Jackson's King Kong" breaks linking. Build through a junction instead.
set "SRC=%~dp0"
set "ROOT=%~dp0.."
echo "%SRC%" | find "'" >nul
if not errorlevel 1 (
  set "LINK=%TEMP%\kk_build_root"
  if exist "!LINK!" rmdir "!LINK!"
  mklink /J "!LINK!" "!ROOT!" >nul || exit /b 1
  set "SRC=!LINK!\kk\"
)
cd /d "%SRC%"
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET% || exit /b 1



