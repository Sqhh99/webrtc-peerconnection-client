@echo off
setlocal enableextensions enabledelayedexpansion

set "CONFIG_NAME=%~1"
if "%CONFIG_NAME%"=="" set "CONFIG_NAME=release"

if /I "%CONFIG_NAME%"=="release" (
    set "BUILD_TYPE=Release"
) else if /I "%CONFIG_NAME%"=="debug" (
    set "BUILD_TYPE=Debug"
) else (
    echo Usage: %~nx0 [release^|debug] [additional CMake configure args...]
    exit /b 1
)

shift

set "EXTRA_CMAKE_ARGS="
:collect_extra_args
if "%~1"=="" goto args_done
set "EXTRA_CMAKE_ARGS=!EXTRA_CMAKE_ARGS! %1"
shift
goto collect_extra_args
:args_done

for %%I in ("%~dp0.") do set "ROOT_DIR=%%~fI"
set "BUILD_DIR=%ROOT_DIR%\build\%CONFIG_NAME%"

if not defined WEBRTC_VERSION set "WEBRTC_VERSION=m146.7680.1.0"
if not defined WEBRTC_PLATFORM set "WEBRTC_PLATFORM=windows_x86_64"
if not defined FFMPEG_RELEASE_TAG set "FFMPEG_RELEASE_TAG=autobuild-2026-03-15-12-59"
if not defined FFMPEG_PACKAGE_VERSION set "FFMPEG_PACKAGE_VERSION=n7.1.3-43-g5a1f107b4c"
if not defined FFMPEG_PACKAGE_TARGET set "FFMPEG_PACKAGE_TARGET=win64-lgpl-shared-7.1"
if not defined VCPKG_TARGET_TRIPLET set "VCPKG_TARGET_TRIPLET=x64-windows-static"

call :ensure_vs_env
if errorlevel 1 exit /b 1

set "PROJECT_VCPKG_ROOT=%ROOT_DIR%\third_party\vcpkg"
set "PROJECT_VCPKG_INSTALLED_DIR=%ROOT_DIR%\third_party\vcpkg_installed"
set "PROJECT_VCPKG_TOOLCHAIN=%PROJECT_VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"

call :find_cmake
if errorlevel 1 exit /b 1

call :find_ninja
if errorlevel 1 exit /b 1

call :ensure_vcpkg
if errorlevel 1 exit /b 1

call :reset_stale_cmake_cache
if errorlevel 1 exit /b 1

where clang-cl >nul 2>nul
if errorlevel 1 (
    echo [build.cmd] clang-cl was not found in PATH. Install LLVM support in Visual Studio or open a shell with clang-cl available.
    exit /b 1
)

call cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_MAKE_PROGRAM:FILEPATH="%NINJA_EXE%" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_CXX_COMPILER=clang-cl ^
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH="%PROJECT_VCPKG_TOOLCHAIN%" ^
    -DVCPKG_TARGET_TRIPLET=%VCPKG_TARGET_TRIPLET% ^
    -DVCPKG_INSTALLED_DIR:PATH="%PROJECT_VCPKG_INSTALLED_DIR%" ^
    -DWEBRTC_VERSION=%WEBRTC_VERSION% ^
    -DWEBRTC_PLATFORM=%WEBRTC_PLATFORM% ^
    -DFFMPEG_RELEASE_TAG=%FFMPEG_RELEASE_TAG% ^
    -DFFMPEG_PACKAGE_VERSION=%FFMPEG_PACKAGE_VERSION% ^
    -DFFMPEG_PACKAGE_TARGET=%FFMPEG_PACKAGE_TARGET% ^
    !EXTRA_CMAKE_ARGS!
if errorlevel 1 exit /b 1

call cmake --build "%BUILD_DIR%" --config %BUILD_TYPE%
exit /b %errorlevel%

:find_cmake
for /f "usebackq delims=" %%I in (`where cmake.exe 2^>nul`) do (
    set "CMAKE_EXE=%%~fI"
    goto cmake_found
)

echo [build.cmd] cmake.exe was not found in PATH.
exit /b 1

:cmake_found
exit /b 0

:find_ninja
set "NINJA_EXE="
set "NINJA_FALLBACK="

for /f "usebackq delims=" %%I in (`where ninja.exe 2^>nul`) do (
    if not defined NINJA_FALLBACK set "NINJA_FALLBACK=%%~fI"
    echo %%~fI | findstr /I "\\depot_tools\\" >nul
    if errorlevel 1 (
        set "NINJA_EXE=%%~fI"
        goto ninja_found
    )
)

if defined NINJA_FALLBACK set "NINJA_EXE=%NINJA_FALLBACK%"

:ninja_found
if not defined NINJA_EXE (
    echo [build.cmd] ninja.exe was not found in PATH.
    exit /b 1
)

exit /b 0

:ensure_vcpkg
if exist "%PROJECT_VCPKG_ROOT%\vcpkg.exe" (
    if exist "%PROJECT_VCPKG_TOOLCHAIN%" exit /b 0
)

where git.exe >nul 2>nul
if errorlevel 1 (
    echo [build.cmd] git.exe was not found in PATH. Install Git for Windows to bootstrap vcpkg.
    exit /b 1
)

if not exist "%PROJECT_VCPKG_ROOT%\.git" (
    echo [build.cmd] Cloning vcpkg into third_party\vcpkg ...
    git clone https://github.com/microsoft/vcpkg.git "%PROJECT_VCPKG_ROOT%"
    if errorlevel 1 (
        echo [build.cmd] Failed to clone vcpkg.
        exit /b 1
    )
)

if not exist "%PROJECT_VCPKG_ROOT%\vcpkg.exe" (
    echo [build.cmd] Bootstrapping vcpkg ...
    call "%PROJECT_VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
    if errorlevel 1 (
        echo [build.cmd] Failed to bootstrap vcpkg.
        exit /b 1
    )
)

if not exist "%PROJECT_VCPKG_TOOLCHAIN%" (
    echo [build.cmd] vcpkg toolchain file was not found after bootstrap.
    exit /b 1
)

exit /b 0

:reset_stale_cmake_cache
set "CACHE_FILE=%BUILD_DIR%\CMakeCache.txt"
if not exist "%CACHE_FILE%" exit /b 0

set "CACHED_NINJA="
set "CACHED_TOOLCHAIN="
for /f "usebackq tokens=2 delims==" %%I in (`findstr /B /C:"CMAKE_MAKE_PROGRAM:FILEPATH=" "%CACHE_FILE%"`) do (
    set "CACHED_NINJA=%%I"
)
for /f "usebackq tokens=2 delims==" %%I in (`findstr /B /C:"CMAKE_TOOLCHAIN_FILE:FILEPATH=" "%CACHE_FILE%"`) do (
    set "CACHED_TOOLCHAIN=%%I"
)

if /I "%CACHED_NINJA%"=="%NINJA_EXE%" if /I "%CACHED_TOOLCHAIN%"=="%PROJECT_VCPKG_TOOLCHAIN%" exit /b 0

echo [build.cmd] Removing stale CMake cache in %BUILD_DIR%
if exist "%BUILD_DIR%\CMakeCache.txt" del /f /q "%BUILD_DIR%\CMakeCache.txt" >nul
if exist "%BUILD_DIR%\CMakeFiles" rmdir /s /q "%BUILD_DIR%\CMakeFiles"
if exist "%BUILD_DIR%\build.ninja" del /f /q "%BUILD_DIR%\build.ninja" >nul
if exist "%BUILD_DIR%\cmake_install.cmake" del /f /q "%BUILD_DIR%\cmake_install.cmake" >nul

exit /b 0

:ensure_vs_env
if defined VSCMD_VER exit /b 0

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [build.cmd] vswhere.exe not found. Launch from a Developer Command Prompt or install Visual Studio Build Tools.
    exit /b 1
)

for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_INSTALL=%%I"
)

if not defined VS_INSTALL (
    echo [build.cmd] Visual Studio with C++ build tools was not found.
    exit /b 1
)

call "%VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 (
    echo [build.cmd] Failed to initialize the Visual Studio developer environment.
    exit /b 1
)

exit /b 0
