param(
    [string]$FFmpegPath = "C:\Program Files\ffmpeg",
    [switch]$Static
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
    # elseif ($count -gt 1 -and -not $Static) { # More than one file and non-static mode: do nothing }
}

# Path to the vendored libcurl used when building dynamically
$VendoredCurl = Join-Path $PSScriptRoot 'vendor\libcurl'

# Recommended FFmpeg install command:
# vcpkg install ffmpeg[dav1d,openh264,x264,x265,mp3lame,fdk-aac,opus,zlib,ffmpeg]:x64-windows-static
# If static build is requested and the default path wasn't changed,
# try to guess the static vcpkg location.
if ($Static -and $FFmpegPath -eq "C:\Program Files\ffmpeg") {
    $guess = "C:\tools\vcpkg\installed\x64-windows-static"
    if (Test-Path "$guess\lib\avcodec.lib") {
        Write-Host "Static mode detected. Using FFmpeg from $guess" -ForegroundColor Cyan
        $FFmpegPath = $guess
    } else {
        Write-Host "Static mode requested but FFmpeg not found in $guess." -ForegroundColor Yellow
    }
}

# Validate that the installation is truly static
if ($Static) {
    if (-not (Test-Path "$FFmpegPath\lib\avcodec.lib")) {
        Write-Host "ERROR: No static FFmpeg installation found at $FFmpegPath" -ForegroundColor Red
        Write-Host "Install 'ffmpeg[dav1d,openh264,x264,x265,mp3lame,fdk-aac,opus,zlib]:x64-windows-static' with vcpkg or specify the path with -FFmpegPath" -ForegroundColor Yellow
        exit 1
    }
    $dlls = Get-ChildItem "$FFmpegPath\bin" -Filter "avcodec*.dll" -ErrorAction SilentlyContinue
    if ($dlls) {
        Write-Host "ERROR: The path $FFmpegPath contains FFmpeg DLLs. A static build is required." -ForegroundColor Red
        Write-Host "Make sure you installed the x64-windows-static triplet" -ForegroundColor Yellow
        exit 1
    }
}

Write-Host "Video Editor Build Script" -ForegroundColor Green
Write-Host "=========================" -ForegroundColor Green

function Add-ToPath {
    param($dir)
    if (Test-Path $dir -and -not ($env:Path -split ';' | Where-Object { $_ -eq $dir })) {
        Write-Host "Adding '$dir' to PATH for this session..." -ForegroundColor Cyan
        $env:Path = "$env:Path;$dir"
    }
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
            Add-ToPath (Split-Path $cmakeExe)
            if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
                Write-Host "❌ CMake not found after installation. Please restart the console." -ForegroundColor Red
                exit 1
            }
            Write-Host "✅ CMake installed and ready." -ForegroundColor Green
        } else {
            Write-Host "Please install CMake manually: https://cmake.org/download/" -ForegroundColor Cyan
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
    Write-Host "Install Build Tools for Windows 11 with winget? (Requires admin) [y/n]" -ForegroundColor Yellow
    if ((Read-Host) -eq "y") {
        Start-Process winget -ArgumentList 'install','--id','Microsoft.VisualStudio.2022.BuildTools','-e','--override','"--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --includeRecommended"' -Verb RunAs -Wait
        if (-not $msbuildPath) {
            Write-Host "❌ MSBuild still not found. Please restart or install manually." -ForegroundColor Red
            exit 1
        }
        Write-Host "✅ MSBuild found at: $msbuildPath" -ForegroundColor Green
    } else {
        Write-Host "Download from: https://visualstudio.microsoft.com/visual-cpp-build-tools/" -ForegroundColor Cyan
        exit 1
    }
} else {
    Write-Host "✅ MSBuild found at: $msbuildPath" -ForegroundColor Green
}

# Default path if not specified
if (-not $FFmpegPath) {
    $FFmpegPath = "$PSScriptRoot\third_party\ffmpeg"
}

# 3) Auto-download only in shared mode
if (-not $Static.IsPresent) {
    if (-not (Test-Path "$FFmpegPath\bin\ffmpeg.exe")) {
        Write-Host "FFmpeg not found in bin/. Downloading..." -ForegroundColor Yellow
        $ffmpegUrl   = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip"
        $zipPath     = "$env:TEMP\ffmpeg.zip"
        $extractPath = "$PSScriptRoot\third_party"

        # Use Start-BitsTransfer if available as it's faster and more reliable
        if (Get-Command Start-BitsTransfer -ErrorAction SilentlyContinue) {
            try {
                Write-Host "Downloading FFmpeg with Start-BitsTransfer..." -ForegroundColor Cyan
                Start-BitsTransfer -Source $ffmpegUrl -Destination $zipPath -ErrorAction Stop
            } catch {
                Write-Host "Start-BitsTransfer failed, using Invoke-WebRequest..." -ForegroundColor Yellow
                Invoke-WebRequest -Uri $ffmpegUrl -OutFile $zipPath
            }
        } else {
            Invoke-WebRequest -Uri $ffmpegUrl -OutFile $zipPath
        }
        Expand-Archive -Path $zipPath -DestinationPath $extractPath -Force

        $extracted = Get-ChildItem -Directory -Path $extractPath | Where-Object Name -Like "ffmpeg*" | Select-Object -First 1
        if ($extracted) {
            Move-Item "$extractPath\$($extracted.Name)" "$FFmpegPath" -Force
            Remove-Item $zipPath -Force
            Write-Host "✅ FFmpeg downloaded and extracted to $FFmpegPath" -ForegroundColor Green
        } else {
            Write-Host "❌ Error extracting FFmpeg." -ForegroundColor Red
            exit 1
        }
    }
}

# 4) Verify FFmpeg and libcurl components based on mode
$curlRoot = $FFmpegPath
if (-not $Static.IsPresent -and -not (Test-Path "$FFmpegPath\lib\libcurl.lib")) {
    if (Test-Path "$VendoredCurl\lib\libcurl.lib") {
        Write-Host "libcurl not found in FFmpeg. Using vendored copy..." -ForegroundColor Yellow
        $curlRoot = $VendoredCurl
    }
}
$env:CURL_ROOT = $curlRoot

if ($Static.IsPresent) {
    $required = @(
        "$FFmpegPath\include\libavcodec\avcodec.h",
        "$FFmpegPath\lib\avcodec.lib",
        "$curlRoot\include\curl\curl.h",
        "$curlRoot\lib\libcurl.lib"
    )
} else {
    $required = @(
        "$FFmpegPath\include\libavcodec\avcodec.h",
        "$FFmpegPath\lib\avcodec.lib",
        "$FFmpegPath\bin\ffmpeg.exe",
        "$curlRoot\include\curl\curl.h",
        "$curlRoot\lib\libcurl.lib"
    )
    if ($curlRoot -eq $VendoredCurl) {
        $required += "$curlRoot\lib\zlib.lib"
    }
}

foreach ($p in $required) {
    if (-not (Test-Path $p)) {
        Write-Host "ERROR: Required component not found at: $p" -ForegroundColor Red
        Write-Host "Ensure that libraries (FFmpeg, curl, etc.) are installed via vcpkg and that the path is correct." -ForegroundColor Yellow
        exit 1
    }
}
Write-Host "FFmpeg validated at: $FFmpegPath" -ForegroundColor Green

# 5) Set vcpkg toolchain
if ($Static.IsPresent) {
    if (-not (Test-Path "C:/tools/vcpkg")) {
        Write-Host "vcpkg not found. Cloning and bootstrapping..." -ForegroundColor Yellow
        git clone https://github.com/microsoft/vcpkg "C:/tools/vcpkg"
        & "C:/tools/vcpkg/bootstrap-vcpkg.bat"
    }
    $env:CMAKE_TOOLCHAIN_FILE = "C:/tools/vcpkg/scripts/buildsystems/vcpkg.cmake"
    Write-Host "Using vcpkg toolchain: $env:CMAKE_TOOLCHAIN_FILE" -ForegroundColor Cyan
} else {
    # Elimina la variable y cache de build para modo dinámico
    if (Test-Path Env:\CMAKE_TOOLCHAIN_FILE) {
        Remove-Item Env:\CMAKE_TOOLCHAIN_FILE -ErrorAction SilentlyContinue
        Write-Host "Not using vcpkg toolchain (dynamic mode)" -ForegroundColor Cyan
    }
    if (Test-Path ".\build") {
        Write-Host "Dynamic mode: removing existing build directory to drop vcpkg cache…" -ForegroundColor Yellow
        Remove-Item ".\build" -Recurse -Force
    }
}

# 5) Configure/reconfigure CMake
$staticFlag = if ($Static.IsPresent) { "ON" } else { "OFF" }
Write-Host "CMake arguments: -DUSE_STATIC_FFMPEG=$staticFlag, -DFFMPEG_ROOT=$FFmpegPath, -DCURL_ROOT=$env:CURL_ROOT" -ForegroundColor Cyan
if (-not (Test-Path ".\build")) {
    Write-Host "Configuring CMake..." -ForegroundColor Yellow
    cmake -S . -B build -D "USE_STATIC_FFMPEG:BOOL=$staticFlag" "-DFFMPEG_ROOT=$FFmpegPath" "-DCURL_ROOT=$env:CURL_ROOT"
} else {
    Write-Host "Reconfiguring CMake with new parameters..." -ForegroundColor Yellow
    cmake -S . -B build -D "USE_STATIC_FFMPEG:BOOL=$staticFlag" "-DFFMPEG_ROOT=$FFmpegPath" "-DCURL_ROOT=$env:CURL_ROOT"
}
if ($LASTEXITCODE -ne 0) { Write-Error "CMake configuration failed."; exit 1 }

# 6) Build the project
Write-Host "Building project..." -ForegroundColor Yellow
cmake --build build --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with CMake."
    exit 1
}

# 7) Copy DLLs only in shared mode
if (-not $Static.IsPresent) {
    $dllSrc = "$FFmpegPath\bin"
    $dllDst = ".\build\Release"
    if (Test-Path $dllDst) {
        Write-Host "Copying FFmpeg DLLs..." -ForegroundColor Yellow
        Get-ChildItem $dllSrc -Filter "*.dll" |
          Where-Object { $_.Name -match "^(avcodec|avformat|avutil|swscale|swresample)" } |
          ForEach-Object {
              Copy-Item $_.FullName -Destination $dllDst -Force
              Write-Host "  Copied: $($_.Name)" -ForegroundColor Cyan
          }
    }
}

# 8) Final executable
$exe = ".\build\Release\VideoEditor.exe"
if (-not (Test-Path $exe)) {
    Write-Host "ERROR: VideoEditor.exe was not created!" -ForegroundColor Red
    exit 1
}

Write-Host "`n✅ Build complete. Launching Video Editor..." -ForegroundColor Green
Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe -Parent)
