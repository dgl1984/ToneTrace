@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "SOURCE_DIR=%SCRIPT_DIR%"
set "FAILED_STEP="
set "BUILD_ERROR=1"
set "BUILD_SCRIPT_VERSION=2026-08-22a"

if not "%SCRIPT_DIR:!=%"=="%SCRIPT_DIR%" (
  echo Tone Trace complete Windows build
  echo Builder revision: %BUILD_SCRIPT_VERSION%
  echo.
  echo ERROR: The project path contains an exclamation mark.
  echo Move the extracted Tone Trace folder to a path without ! and run again.
  echo This window will remain open until you press a key.
  echo.
  pause
  exit /b 8
)

if not exist "%SOURCE_DIR%\CMakeLists.txt" (
  if exist "%SCRIPT_DIR%\tone_trace_clean\CMakeLists.txt" (
    set "SOURCE_DIR=%SCRIPT_DIR%\tone_trace_clean"
  ) else (
    set "FAILED_STEP=Locating the Tone Trace source tree"
    set "BUILD_ERROR=1"
    echo Tone Trace complete Windows build
    echo Builder revision: %BUILD_SCRIPT_VERSION%
    echo.
    echo ERROR: CMakeLists.txt was not found beside this builder or in
    echo "%SCRIPT_DIR%\tone_trace_clean".
    echo Extract the complete Tone Trace source package before running the builder.
    goto :build_failed
  )
)

setlocal EnableDelayedExpansion

if not exist "%SOURCE_DIR%\VERSION" (
  set "FAILED_STEP=Reading the project version"
  set "BUILD_ERROR=1"
  echo ERROR: VERSION is missing from the source tree.
  goto :build_failed
)
set /p PROJECT_VERSION=<"%SOURCE_DIR%\VERSION"
echo(!PROJECT_VERSION!| findstr /r /x "[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*" >nul
if not "!errorlevel!"=="0" (
  set "FAILED_STEP=Validating the project version"
  set "BUILD_ERROR=1"
  echo ERROR: VERSION must contain a semantic version such as 1.0.0.
  goto :build_failed
)

set "BUILD_DIR=%SOURCE_DIR%\build-cmake\windows-x64-release"
set "DIST_DIR=%SOURCE_DIR%\dist"
set "PACKAGE_NAME=ToneTrace_EQ_!PROJECT_VERSION!_Windows_x64"
set "STAGE_DIR=%DIST_DIR%\%PACKAGE_NAME%"
set "ZIP_PATH=%DIST_DIR%\%PACKAGE_NAME%.zip"
set "ZIP_HASH_PATH=%ZIP_PATH%.sha256"
set "BUILD_LOG=%SOURCE_DIR%\ToneTrace_Windows_last_build.log"

> "%BUILD_LOG%" echo Tone Trace Windows build log
>> "%BUILD_LOG%" echo Builder revision: %BUILD_SCRIPT_VERSION%
>> "%BUILD_LOG%" echo Source: "%SOURCE_DIR%"
>> "%BUILD_LOG%" echo Started: %DATE% %TIME%
>> "%BUILD_LOG%" echo.

echo Tone Trace complete Windows build
echo Builder revision: %BUILD_SCRIPT_VERSION%
echo Source: "%SOURCE_DIR%"
echo.

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  for /f "delims=" %%I in ('where vswhere.exe 2^>nul') do if not defined VSWHERE_FOUND set "VSWHERE_FOUND=%%I"
  if defined VSWHERE_FOUND set "VSWHERE=!VSWHERE_FOUND!"
)
if not exist "%VSWHERE%" (
  set "FAILED_STEP=Locating Visual Studio Build Tools"
  set "BUILD_ERROR=2"
  echo ERROR: Visual Studio Installer's vswhere.exe was not found.
  echo Install Visual Studio 2022 Build Tools with Desktop development with C++.
  goto :build_failed
)

set "VSWHERE_TMP=%TEMP%\tonetrace_vswhere_install_path.txt"
"%VSWHERE%" -latest -version "[17.0,18.0)" -products * -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "!VSWHERE_TMP!" 2>nul
set "VS_INSTALLATION="
for /f "usebackq delims=" %%I in ("!VSWHERE_TMP!") do if not defined VS_INSTALLATION set "VS_INSTALLATION=%%I"
del "!VSWHERE_TMP!" >nul 2>nul
if not defined VS_INSTALLATION (
  set "FAILED_STEP=Locating a Visual Studio C++ toolchain"
  set "BUILD_ERROR=3"
  echo ERROR: No usable Visual Studio 2022 C++ installation was found.
  echo Add Desktop development with C++ and a Windows 10 or 11 SDK.
  goto :build_failed
)
echo Visual Studio: !VS_INSTALLATION!

set "VSDEVCMD=!VS_INSTALLATION!\Common7\Tools\VsDevCmd.bat"
if not exist "!VSDEVCMD!" (
  set "FAILED_STEP=Locating the Visual Studio developer environment"
  set "BUILD_ERROR=4"
  echo ERROR: VsDevCmd.bat was not found beneath the selected Visual Studio installation.
  echo Repair Visual Studio 2022 Build Tools and include Desktop development with C++.
  goto :build_failed
)

echo Initializing the Visual Studio x64 compiler environment...
rem Normalize the inherited variable name before VsDevCmd. Some launchers pass
rem a mixed-case Path; VsDevCmd adds PATH, and MSBuild then rejects the duplicate
rem case-insensitive key when it starts cl.exe.
set "NORMALIZED_PATH=!PATH!"
set "Path="
set "PATH=!NORMALIZED_PATH!"
set "NORMALIZED_PATH="
call "!VSDEVCMD!" -no_logo -arch=x64 -host_arch=x64 >> "!BUILD_LOG!" 2>&1
set "STEP_ERROR=!errorlevel!"
if not "!STEP_ERROR!"=="0" (
  set "FAILED_STEP=Initializing the Visual Studio x64 compiler environment"
  set "BUILD_ERROR=!STEP_ERROR!"
  goto :build_failed
)

if not "!VisualStudioVersion:~0,3!"=="17." (
  set "FAILED_STEP=Selecting Visual Studio 2022"
  set "BUILD_ERROR=5"
  echo ERROR: Visual Studio 2022 version 17.x is required.
  echo Selected Visual Studio version: !VisualStudioVersion!
  goto :build_failed
)

where cl.exe >nul 2>nul
set "STEP_ERROR=!errorlevel!"
if not "!STEP_ERROR!"=="0" (
  set "FAILED_STEP=Verifying the Microsoft C++ compiler"
  set "BUILD_ERROR=!STEP_ERROR!"
  echo ERROR: cl.exe is not available after initializing Visual Studio.
  echo Add the MSVC x64/x86 build tools in Visual Studio Installer.
  goto :build_failed
)

where msbuild.exe >nul 2>nul
set "STEP_ERROR=!errorlevel!"
if not "!STEP_ERROR!"=="0" (
  set "FAILED_STEP=Verifying MSBuild"
  set "BUILD_ERROR=!STEP_ERROR!"
  echo ERROR: msbuild.exe is not available after initializing Visual Studio.
  echo Repair Visual Studio 2022 Build Tools.
  goto :build_failed
)

set "CMAKE_EXE="
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
if not defined CMAKE_EXE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "!VS_INSTALLATION!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=!VS_INSTALLATION!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE (
  set "FAILED_STEP=Locating CMake"
  set "BUILD_ERROR=6"
  echo ERROR: CMake 3.25 or newer was not found.
  echo Install CMake or add the C++ CMake tools component in Visual Studio Installer.
  goto :build_failed
)

for %%I in ("!CMAKE_EXE!") do set "CTEST_EXE=%%~dpIctest.exe"
if not exist "!CTEST_EXE!" (
  set "FAILED_STEP=Locating CTest"
  set "BUILD_ERROR=7"
  echo ERROR: ctest.exe was not found beside CMake.
  echo Repair the selected CMake installation.
  goto :build_failed
)

for /f "delims=" %%I in ('where cl.exe') do if not defined CL_EXE set "CL_EXE=%%I"
echo Compiler: !CL_EXE!
echo CMake:   !CMAKE_EXE!

echo.
echo Removing stale generated output...
"!CMAKE_EXE!" -E remove_directory "%BUILD_DIR%" >> "!BUILD_LOG!" 2>&1
set "STEP_ERROR=!errorlevel!"
if not "!STEP_ERROR!"=="0" (
  set "FAILED_STEP=Removing the previous build directory"
  set "BUILD_ERROR=!STEP_ERROR!"
  goto :build_failed
)
"!CMAKE_EXE!" -E remove_directory "%STAGE_DIR%" >> "!BUILD_LOG!" 2>&1
set "STEP_ERROR=!errorlevel!"
if not "!STEP_ERROR!"=="0" (
  set "FAILED_STEP=Removing the previous staged package"
  set "BUILD_ERROR=!STEP_ERROR!"
  goto :build_failed
)
"!CMAKE_EXE!" -E rm -f "%ZIP_PATH%" "%ZIP_HASH_PATH%" >> "!BUILD_LOG!" 2>&1
set "STEP_ERROR=!errorlevel!"
if not "!STEP_ERROR!"=="0" (
  set "FAILED_STEP=Removing the previous package ZIP"
  set "BUILD_ERROR=!STEP_ERROR!"
  goto :build_failed
)

echo.
echo Configuring Release x64 for Windows 10 compatibility...
"!CMAKE_EXE!" -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_SYSTEM_VERSION=10.0 -DBUILD_TESTING=ON -DTONETRACE_BUILD_TOOLS=ON -DTONETRACE_BUILD_TESTS=ON -DTONETRACE_BUILD_PLUGINS=ON -DTONETRACE_STATIC_MSVC_RUNTIME=ON >> "!BUILD_LOG!" 2>&1
set "STEP_ERROR=!errorlevel!"
if not "!STEP_ERROR!"=="0" (
  set "FAILED_STEP=CMake configuration"
  set "BUILD_ERROR=!STEP_ERROR!"
  goto :build_failed
)

echo.
echo Building Tone Trace CLAP and release verification targets...
"!CMAKE_EXE!" --build "%BUILD_DIR%" --config Release --parallel >> "!BUILD_LOG!" 2>&1
set "STEP_ERROR=!errorlevel!"
if not "!STEP_ERROR!"=="0" (
  set "FAILED_STEP=Release compilation"
  set "BUILD_ERROR=!STEP_ERROR!"
  goto :build_failed
)

echo.
echo Running all registered engine, realtime, layout, state, and CLAP tests...
"!CTEST_EXE!" --test-dir "%BUILD_DIR%" -C Release --output-on-failure >> "!BUILD_LOG!" 2>&1
set "STEP_ERROR=!errorlevel!"
if not "!STEP_ERROR!"=="0" (
  set "FAILED_STEP=Automated tests"
  set "BUILD_ERROR=!STEP_ERROR!"
  goto :build_failed
)

echo.
echo Staging the CLAP plug-in and documentation only...
"!CMAKE_EXE!" --install "%BUILD_DIR%" --config Release --component Release --prefix "%STAGE_DIR%" >> "!BUILD_LOG!" 2>&1
set "STEP_ERROR=!errorlevel!"
if not "!STEP_ERROR!"=="0" (
  set "FAILED_STEP=Staging portable artifacts"
  set "BUILD_ERROR=!STEP_ERROR!"
  goto :build_failed
)

"!CMAKE_EXE!" -DROOT="%STAGE_DIR%" -DSOURCE_ROOT="%SOURCE_DIR%" -DOUT="%STAGE_DIR%\docs\BUILD_MANIFEST.txt" -DPLATFORM_NAME="Windows x64" -DPROJECT_VERSION="!PROJECT_VERSION!" -P "%SOURCE_DIR%\cmake\write_build_manifest.cmake" >> "!BUILD_LOG!" 2>&1
set "STEP_ERROR=!errorlevel!"
if not "!STEP_ERROR!"=="0" (
  set "FAILED_STEP=Writing the build and source manifest"
  set "BUILD_ERROR=!STEP_ERROR!"
  goto :build_failed
)

echo.
echo Creating the portable ZIP...
pushd "%DIST_DIR%"
set "STEP_ERROR=!errorlevel!"
if not "!STEP_ERROR!"=="0" (
  set "FAILED_STEP=Opening the package directory"
  set "BUILD_ERROR=!STEP_ERROR!"
  goto :build_failed
)
"!CMAKE_EXE!" -E tar cf "%ZIP_PATH%" --format=zip "%PACKAGE_NAME%" >> "!BUILD_LOG!" 2>&1
set "ZIP_ERROR=!errorlevel!"
popd
if not "!ZIP_ERROR!"=="0" (
  set "FAILED_STEP=Creating the portable ZIP"
  set "BUILD_ERROR=!ZIP_ERROR!"
  goto :build_failed
)

"!CMAKE_EXE!" -E tar tf "%ZIP_PATH%" >> "!BUILD_LOG!" 2>&1
set "STEP_ERROR=!errorlevel!"
if not "!STEP_ERROR!"=="0" (
  set "FAILED_STEP=Verifying the portable ZIP"
  set "BUILD_ERROR=!STEP_ERROR!"
  goto :build_failed
)

echo.
echo SUCCESS: Tone Trace built, tested, staged, and packaged.
echo Folder: "%STAGE_DIR%"
echo ZIP:    "%ZIP_PATH%"
echo The ZIP is the only release asset. Per-file hashes are in docs\BUILD_MANIFEST.txt.
echo Log:    "%BUILD_LOG%"
echo Nothing was installed on this computer.
echo This window will remain open until you press a key.
echo.
pause
exit /b 0

:build_failed
if not defined FAILED_STEP set "FAILED_STEP=Unknown build step"
if not defined BUILD_ERROR set "BUILD_ERROR=1"
echo.
echo ERROR: Tone Trace did not complete the build.
echo Failed step: %FAILED_STEP%
echo Exit code: %BUILD_ERROR%
echo Review the messages immediately above this notice.
if defined BUILD_LOG (
  echo.
  echo Complete retained log: "%BUILD_LOG%"
  echo.
  type "%BUILD_LOG%"
)
echo No plug-in was installed or replaced.
echo This window will remain open until you press a key.
echo.
pause
exit /b %BUILD_ERROR%
