@echo off
setlocal enabledelayedexpansion

echo ===============================================
echo foobar2000 Artwork Component Build Script
echo ===============================================

REM Check if Visual Studio is installed
set "VS_EDITION="
set "MSBUILD_PATH="

REM Check for Visual Studio 2022 (v143 toolset)
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    set "VS_EDITION=Enterprise 2022"
) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
    set "VS_EDITION=Professional 2022"
) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    set "VS_EDITION=Community 2022"
) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    set "VS_EDITION=Enterprise 2019"
) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe"
    set "VS_EDITION=Professional 2019"
) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set "MSBUILD_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
    set "VS_EDITION=Community 2019"
)

if "!MSBUILD_PATH!"=="" (
    echo ERROR: Visual Studio 2019 or 2022 with MSBuild not found!
    echo Please install Visual Studio 2019 or 2022 with C++ development tools.
    echo.
    echo Required components:
    echo - MSVC v143 - VS 2022 C++ x64/x86 build tools
    echo - MSVC v143 - VS 2022 C++ ATL for v143 build tools
    echo - Windows 10 SDK
    echo - CMake tools for Visual Studio
    echo.
    pause
    exit /b 1
)

echo Found Visual Studio: !VS_EDITION!
echo MSBuild path: !MSBUILD_PATH!
echo.

REM Set default configuration
set "BUILD_CONFIG=Release"
set "BUILD_PLATFORM=x64"

REM Parse command line arguments
:parse_args
if "%1"=="" goto start_build
if /i "%1"=="debug" set "BUILD_CONFIG=Debug"
if /i "%1"=="release" set "BUILD_CONFIG=Release"
if /i "%1"=="clean" set "CLEAN_BUILD=1"
if /i "%1"=="help" goto show_help
if /i "%1"=="/?" goto show_help
if /i "%1"=="-h" goto show_help
shift
goto parse_args

:show_help
echo.
echo Usage: build.bat [options]
echo.
echo Options:
echo   debug       Build Debug configuration (default: Release)
echo   release     Build Release configuration
echo   clean       Clean before building
echo   help        Show this help message
echo.
echo Examples:
echo   build.bat                    Build Release x64
echo   build.bat debug              Build Debug x64
echo   build.bat clean release      Clean and build Release x64
echo.
pause
exit /b 0

:start_build
echo Building Configuration: !BUILD_CONFIG! ^| Platform: !BUILD_PLATFORM!
echo.

REM Change to the directory containing the script
cd /d "%~dp0"

REM Check if solution file exists
if not exist "foo_artwork.sln" (
    echo ERROR: foo_artwork.sln not found in current directory!
    echo Make sure you're running this script from the project root directory.
    pause
    exit /b 1
)

REM Check if foobar2000 SDK exists
if not exist "lib\foobar2000_SDK\foobar2000\SDK\foobar2000_SDK.vcxproj" (
    echo ERROR: foobar2000 SDK not found in lib\foobar2000_SDK\
    echo Please ensure the foobar2000 SDK is properly extracted to the lib directory.
    pause
    exit /b 1
)

REM Clean if requested
if defined CLEAN_BUILD (
    echo Cleaning previous build outputs...
    "!MSBUILD_PATH!" "foo_artwork.sln" /p:Configuration=!BUILD_CONFIG! /p:Platform=!BUILD_PLATFORM! /t:Clean /v:minimal /nologo
    if errorlevel 1 (
        echo WARNING: Clean operation had issues, but continuing...
    )
    echo.
)

REM Create output directory if it doesn't exist
if not exist "!BUILD_CONFIG!" mkdir "!BUILD_CONFIG!"

echo Starting build process...
echo.

echo Building standalone component (no SDK dependencies)...

REM Build the solution
"!MSBUILD_PATH!" "foo_artwork.sln" /p:Configuration=!BUILD_CONFIG! /p:Platform=!BUILD_PLATFORM! /p:PlatformToolset=v143 /maxcpucount /v:normal /nologo

if errorlevel 1 (
    echo.
    echo ===============================================
    echo BUILD FAILED!
    echo ===============================================
    echo.
    echo Common solutions:
    echo 1. Ensure Visual Studio 2019/2022 with C++ tools is installed
    echo 2. Verify Windows 10 SDK is installed
    echo 3. Check that foobar2000 SDK is properly extracted
    echo 4. Try building with Visual Studio IDE first to see detailed errors
    echo.
    pause
    exit /b 1
)

echo.
echo ===============================================
echo BUILD SUCCESSFUL!
echo ===============================================

REM Check if DLL was created
set "DLL_PATH=!BUILD_CONFIG!\foo_artwork.dll"
if exist "!DLL_PATH!" (
    echo.
    echo Output: !DLL_PATH!
    
    REM Get file size
    for %%A in ("!DLL_PATH!") do set "DLL_SIZE=%%~zA"
    echo Size: !DLL_SIZE! bytes
    
    REM Get file timestamp
    for %%A in ("!DLL_PATH!") do set "DLL_DATE=%%~tA"
    echo Created: !DLL_DATE!
    
    echo.
    echo Installation Instructions:
    echo 1. Close foobar2000 completely
    echo 2. Copy !DLL_PATH! to your foobar2000\components\ directory
    echo 3. Start foobar2000
    echo 4. Go to Preferences ^> Tools ^> Artwork Display to configure
    echo.
    
    REM Offer to open output directory
    set /p OPEN_DIR="Open output directory? (y/n): "
    if /i "!OPEN_DIR!"=="y" (
        explorer "!BUILD_CONFIG!"
    )
) else (
    echo.
    echo WARNING: Build completed but foo_artwork.dll not found in expected location.
    echo Check the !BUILD_CONFIG! directory for output files.
)

echo.
echo Press any key to exit...
pause >nul

:error
echo.
echo ===============================================
echo BUILD FAILED!
echo ===============================================
echo.
echo The build encountered errors. Please check the output above for details.
echo.
echo Common solutions:
echo 1. Ensure Visual Studio 2019/2022 with C++ tools AND ATL is installed
echo 2. Install "MSVC v143 - VS 2022 C++ ATL for v143 build tools"
echo 3. Verify Windows 10 SDK is installed
echo 4. Check that foobar2000 SDK is properly extracted
echo.
pause
exit /b 1