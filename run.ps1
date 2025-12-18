param(
    [string]$FFmpegPath = "$PSScriptRoot\third_party\ffmpeg",
    [switch]$Static,
    [string]$QtPath = $env:QT_PATH,
    [switch]$NoLaunch
)

# 0) Clean the build/Release folder based on conditions
$releaseDir = Join-Path $PSScriptRoot 'build\Release'
if (Test-Path $releaseDir) {
    $files = Get-ChildItem -Path $releaseDir -File
    $count = $files.Count

    if ($count -eq 0) {
        # No files, do nothing
    }
    elseif ($count -eq 1 -and $Static) {
        # Static mode and only one file: do nothing
    }
    elseif ($count -eq 1 -and -not $Static) {
        try {
            Write-Host "Switching to dynamic mode..." -ForegroundColor Yellow
            Remove-Item -Path (Join-Path $PSScriptRoot 'build') -Recurse -Force
        } catch {
            Write-Host "ERROR: Could not remove the 'build' folder. Stopping execution." -ForegroundColor Red
            exit 1
        }
    }
    elseif ($count -gt 1 -and $Static) {
        try {
            Write-Host "Switching to static mode..." -ForegroundColor Yellow
            Remove-Item -Path (Join-Path $PSScriptRoot 'build') -Recurse -Force
        } catch {
            Write-Host "ERROR: Could not remove the 'build' folder. Stopping execution." -ForegroundColor Red
            exit 1
        }
    }
}

$VendoredCurl = Join-Path $PSScriptRoot 'vendor\libcurl'

if ($Static -and $FFmpegPath -eq "$PSScriptRoot\third_party\ffmpeg") {
    $guess = "C:\tools\vcpkg\installed\x64-windows-static"
    if (Test-Path "$guess\lib\avcodec.lib") {
        Write-Host "Static mode detected. Using FFmpeg from $guess" -ForegroundColor Cyan
        $FFmpegPath = $guess
    } else {
        Write-Host "Static mode requested but FFmpeg not found in $guess." -ForegroundColor Yellow
    }
}

if ($Static) {
    if (-not (Test-Path "$FFmpegPath\lib\avcodec.lib")) {
        Write-Host "ERROR: No static FFmpeg installation found at $FFmpegPath" -ForegroundColor Red
        Write-Host "Install via vcpkg or specify -FFmpegPath" -ForegroundColor Yellow
        exit 1
    }
    $dlls = Get-ChildItem "$FFmpegPath\bin" -Filter "avcodec*.dll" -ErrorAction SilentlyContinue
    if ($dlls) {
        Write-Host "ERROR: Found DLLs in $FFmpegPath; expected static libs." -ForegroundColor Red
        exit 1
    }
}

Write-Host "Video Editor Build Script" -ForegroundColor Green
Write-Host "=========================" -ForegroundColor Green

function Add-ToPath {
    param($dir)
    if (Test-Path $dir) {
        if (-not ($env:Path -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ -eq $dir })) {
            Write-Host "Adding '$dir' to PATH for this session..." -ForegroundColor Cyan
            $env:Path = "$dir;$env:Path"
        }
    }
}

function Restart-Script {
    Write-Host "`n⚠️  Dependency installed. Restarting script..." -ForegroundColor Yellow
    # Usamos & pwsh con -File para manejar rutas con espacios y no abrir nueva ventana
    $argsList = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$PSCommandPath) + $MyInvocation.UnboundArguments
    & pwsh @argsList
    exit 0
}

# 1) Check for CMake
$cmakeExe = "$env:ProgramFiles\CMake\bin\cmake.exe"
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    if (Test-Path $cmakeExe) {
        Add-ToPath (Split-Path $cmakeExe)
        Write-Host "✅ CMake detected and added to PATH." -ForegroundColor Green
    } else {
        Write-Host "`n❌ CMake is not installed." -ForegroundColor Red
        Write-Host "Install it with winget? (Requires admin) [y/n]" -ForegroundColor Yellow
        if ((Read-Host) -eq "y") {
            Start-Process winget -ArgumentList 'install','Kitware.CMake','-e','--silent' -Verb RunAs -Wait
            if (Test-Path $cmakeExe) {
                Add-ToPath (Split-Path $cmakeExe)
                Restart-Script
            } else {
                Write-Host "❌ CMake not found after install. Restart manually." -ForegroundColor Red
                exit 1
            }
        } else {
            Write-Host "Manually install from: https://cmake.org/download/" -ForegroundColor Cyan
            exit 1
        }
    }
} else {
    Write-Host "✅ CMake available: $(Get-Command cmake).Source" -ForegroundColor Green
}

# 2) Check for MSBuild
function Find-MSBuild {
    $vsPaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($p in $vsPaths) { if (Test-Path $p) { return $p } }
    $found = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022" -Recurse -Filter "MSBuild.exe" -ErrorAction SilentlyContinue
    if ($found) { return $found[0].FullName } else { return $null }
}

$msbuildPath = Find-MSBuild
if (-not $msbuildPath) {
    Write-Host "`n❌ MSBuild not found." -ForegroundColor Red
    Write-Host "Install BuildTools via winget? [y/n]" -ForegroundColor Yellow
    if ((Read-Host) -eq "y") {
        Start-Process winget -ArgumentList 'install','--id','Microsoft.VisualStudio.2022.BuildTools','-e','--override','"--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --includeRecommended"' -Verb RunAs -Wait
        $msbuildPath = Find-MSBuild
        if ($msbuildPath) {
            Restart-Script
        } else {
            Write-Host "❌ MSBuild still not found. Restart manually." -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "Download from: https://visualstudio.microsoft.com/visual-cpp-build-tools/" -ForegroundColor Cyan
        exit 1
    }
} else {
    Write-Host "✅ MSBuild found at: $msbuildPath" -ForegroundColor Green
}

# Default FFmpegPath if not specified
if (-not $FFmpegPath) { $FFmpegPath = "$PSScriptRoot\third_party\ffmpeg" }

# 3) Auto-download only in shared mode
if (-not $Static.IsPresent) {
    if (-not (Test-Path "$FFmpegPath\bin\ffmpeg.exe")) {
        Write-Host "FFmpeg not found. Downloading..." -ForegroundColor Yellow
        $ffmpegUrl   = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip"
        $zipPath     = "$env:TEMP\ffmpeg.zip"
        $extractPath = "$PSScriptRoot\third_party"

        if (Get-Command Start-BitsTransfer -ErrorAction SilentlyContinue) {
            try {
                Write-Host "Downloading with Start-BitsTransfer..." -ForegroundColor Cyan
                & Start-BitsTransfer -Source "$ffmpegUrl" -Destination "$zipPath" -ErrorAction Stop
            } catch {
                Write-Host "Fallback to Invoke-WebRequest..." -ForegroundColor Yellow
                & Invoke-WebRequest -Uri "$ffmpegUrl" -OutFile "$zipPath"
            }
        } else {
            & Invoke-WebRequest -Uri "$ffmpegUrl" -OutFile "$zipPath"
        }

        & Expand-Archive -Path "$zipPath" -DestinationPath "$extractPath" -Force
        $extracted = Get-ChildItem -Directory -Path "$extractPath" | Where-Object Name -Like "ffmpeg*" | Select-Object -First 1
        if ($extracted) {
            & Move-Item -Path "$extractPath\$($extracted.Name)" -Destination "$FFmpegPath" -Force
            & Remove-Item -Path "$zipPath" -Force
            Write-Host "✅ FFmpeg extracted to $FFmpegPath" -ForegroundColor Green
        } else {
            Write-Host "❌ Error extracting FFmpeg." -ForegroundColor Red
            exit 1
        }
    }
}

# 4) Verify FFmpeg & libcurl
$curlRoot = $FFmpegPath
if (-not $Static.IsPresent -and -not (Test-Path "$FFmpegPath\lib\libcurl.lib")) {
    if (Test-Path "$VendoredCurl\lib\libcurl.lib") {
        Write-Host "Using vendored curl..." -ForegroundColor Yellow
        $curlRoot = $VendoredCurl
    }
}
$env:CURL_ROOT = $curlRoot

$required = @(
    "$FFmpegPath\include\libavcodec\avcodec.h",
    "$FFmpegPath\lib\avcodec.lib",
    "$curlRoot\include\curl\curl.h",
    "$curlRoot\lib\libcurl.lib"
)
if (-not $Static.IsPresent) {
    $required += "$FFmpegPath\bin\ffmpeg.exe"
    if ($curlRoot -eq $VendoredCurl) { $required += "$curlRoot\lib\zlib.lib" }
}

foreach ($p in $required) {
    if (-not (Test-Path "$p")) {
        Write-Host "ERROR: Missing: $p" -ForegroundColor Red
        exit 1
    }
}
Write-Host "FFmpeg validated at: $FFmpegPath" -ForegroundColor Green

# Ensure the app isn't running (avoid linker/file-lock issues). In CI this process
# usually doesn't exist, so swallow any errors.
Get-Process -Name VideoEditor -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
# 5) vcpkg toolchain
if ($Static.IsPresent) {
    if (-not (Test-Path "C:\tools\vcpkg")) {
        Write-Host "Bootstrapping vcpkg..." -ForegroundColor Yellow
        & git clone "https://github.com/microsoft/vcpkg" "C:\tools\vcpkg"
        & "C:\tools\vcpkg\bootstrap-vcpkg.bat"
    }
    $env:CMAKE_TOOLCHAIN_FILE = "C:\tools\vcpkg\scripts\buildsystems\vcpkg.cmake"
    Write-Host "Using vcpkg toolchain." -ForegroundColor Cyan
} else {
    if (Test-Path Env:\CMAKE_TOOLCHAIN_FILE) {
        Remove-Item Env:\CMAKE_TOOLCHAIN_FILE -ErrorAction SilentlyContinue
        Write-Host "Dynamic mode: no toolchain." -ForegroundColor Cyan
    }
    # Removed forced cleaning to allow incremental builds
}

# 6) Configure CMake
$staticFlag = if ($Static.IsPresent) { "ON" } else { "OFF" }
Write-Host "Config: USE_STATIC=$staticFlag, FFMPEG=$FFmpegPath" -ForegroundColor Cyan

if ($QtPath -and (Test-Path $QtPath)) {
    # If the user pointed to the root (e.g. C:\Qt), try to find a specific kit folder
    if (-not (Test-Path "$QtPath\lib\cmake")) {
        $foundKit = Get-ChildItem -Path $QtPath -Recurse -Depth 2 -Directory | 
                    Where-Object { $_.Name -match '^(msvc|mingw)' -and (Test-Path "$($_.FullName)\lib\cmake") } | 
                    Select-Object -First 1
        if ($foundKit) {
            $QtPath = $foundKit.FullName
            Write-Host "Auto-detected Qt kit: $QtPath" -ForegroundColor Cyan
        }
    }
    Write-Host "Qt: $QtPath" -ForegroundColor Cyan
}

if (-not (Test-Path ".\build")) {
    Write-Host "Configuring CMake..." -ForegroundColor Yellow
    $cmakeArgs = @(
        "-S", ".", "-B", "build",
        "-DUSE_STATIC_FFMPEG:BOOL=$staticFlag",
        "-DFFMPEG_ROOT=$FFmpegPath",
        "-DCURL_ROOT=$env:CURL_ROOT",
        "-UQt6_*", "-UQt5_*", "-UQt6_DIR", "-UQt5_DIR"
    )
    if ($QtPath) { $cmakeArgs += "-DCMAKE_PREFIX_PATH=$QtPath" }
    & cmake @cmakeArgs
} else {
    Write-Host "Reconfiguring CMake..." -ForegroundColor Yellow
    $cmakeArgs = @(
        "-S", ".", "-B", "build",
        "-DUSE_STATIC_FFMPEG:BOOL=$staticFlag",
        "-DFFMPEG_ROOT=$FFmpegPath",
        "-DCURL_ROOT=$env:CURL_ROOT",
        "-UQt6_*", "-UQt5_*", "-UQt6_DIR", "-UQt5_DIR"
    )
    if ($QtPath) { $cmakeArgs += "-DCMAKE_PREFIX_PATH=$QtPath" }
    & cmake @cmakeArgs
}
if ($LASTEXITCODE -ne 0) { Write-Error "CMake failed"; exit 1 }

# 7) Build
Write-Host "Building..." -ForegroundColor Yellow
& cmake --build "build" --config "Release"
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed"; exit 1 }

# 8) Copy DLLs in shared mode
if (-not $Static.IsPresent) {
    $dllSrc = "$FFmpegPath\bin"
    $dllDst = ".\build\Release"
    if (Test-Path "$dllDst") {
        Write-Host "Copying FFmpeg DLLs..." -ForegroundColor Yellow
        Get-ChildItem -Path "$dllSrc" -Filter "*.dll" |
          Where-Object { $_.Name -match "^(avcodec|avformat|avutil|swscale|swresample)" } |
          ForEach-Object {
              Copy-Item -LiteralPath $_.FullName -Destination "$dllDst" -Force
              Write-Host "  Copied: $($_.Name)" -ForegroundColor Cyan
          }
    }
}

# 9) Launch
$exe = ".\build\Release\VideoEditor.exe"
if (-not (Test-Path "$exe")) {
    Write-Host "ERROR: No executable found." -ForegroundColor Red
    exit 1
}

# Deploy Qt runtime so the executable can run outside a Qt environment.
# This fixes errors like "Qt6Core.dll/Qt6Gui.dll/Qt6Widgets.dll was not found".
if ($QtPath) {
    $windeployqt = Join-Path $QtPath "bin\\windeployqt.exe"
    if (Test-Path $windeployqt) {
        Write-Host "Deploying Qt runtime (windeployqt)..." -ForegroundColor Yellow
        & $windeployqt --release --compiler-runtime --no-translations --no-opengl-sw "$exe"
        if ($LASTEXITCODE -ne 0) {
            Write-Host "WARNING: windeployqt failed (exit $LASTEXITCODE). The app may still fail to launch due to missing Qt DLLs." -ForegroundColor Yellow
        }
    } else {
        Write-Host "WARNING: windeployqt not found at: $windeployqt" -ForegroundColor Yellow
        Write-Host "Install the full Qt kit (includes windeployqt) or add Qt's bin folder to PATH." -ForegroundColor Yellow
    }
}
Write-Host ""
Write-Host "✅ Build complete. Launching..." -ForegroundColor Green
if (-not $NoLaunch.IsPresent) {
    Start-Process -FilePath "$exe" -WorkingDirectory (Split-Path "$exe" -Parent)
} else {
    Write-Host "(Skipped launch: -NoLaunch)" -ForegroundColor Yellow
}
