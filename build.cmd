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

if not defined QT_ROOT if exist "d:\Qt\6.10.0\msvc2022_64" set "QT_ROOT=d:\Qt\6.10.0\msvc2022_64"

call :ensure_vs_env
if errorlevel 1 exit /b 1

call :find_cmake
if errorlevel 1 exit /b 1

call :find_ninja
if errorlevel 1 exit /b 1

call :reset_stale_cmake_cache
if errorlevel 1 exit /b 1

where clang-cl >nul 2>nul
if errorlevel 1 (
    echo [build.cmd] clang-cl was not found in PATH. Install LLVM support in Visual Studio or open a shell with clang-cl available.
    exit /b 1
)

set "QT_ARG="
if defined QT_ROOT set "QT_ARG=-DCMAKE_PREFIX_PATH=%QT_ROOT%"

call cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_MAKE_PROGRAM:FILEPATH="%NINJA_EXE%" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_C_COMPILER=clang-cl ^
    -DCMAKE_CXX_COMPILER=clang-cl ^
    -DWEBRTC_VERSION=%WEBRTC_VERSION% ^
    -DWEBRTC_PLATFORM=%WEBRTC_PLATFORM% ^
    %QT_ARG% ^
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

:reset_stale_cmake_cache
set "CACHE_FILE=%BUILD_DIR%\CMakeCache.txt"
if not exist "%CACHE_FILE%" exit /b 0

set "CACHED_NINJA="
for /f "usebackq tokens=2 delims==" %%I in (`findstr /B /C:"CMAKE_MAKE_PROGRAM:FILEPATH=" "%CACHE_FILE%"`) do (
    set "CACHED_NINJA=%%I"
)

if not defined CACHED_NINJA exit /b 0
if /I "%CACHED_NINJA%"=="%NINJA_EXE%" exit /b 0

echo [build.cmd] Removing stale CMake cache that points at: %CACHED_NINJA%
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
