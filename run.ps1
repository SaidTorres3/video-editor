# PowerShell script to build and run the Video Editor

param(
    [string]$FFmpegPath = "C:\Program Files\ffmpeg"
)

Write-Host "Video Editor Build Script" -ForegroundColor Green
Write-Host "=========================" -ForegroundColor Green

# Define default FFmpeg path
if (-not $FFmpegPath) {
    $FFmpegPath = "$PSScriptRoot\third_party\ffmpeg"
}

# Auto-download FFmpeg if not found (ONLY WORKS FOR WINDOWS)
if (-not (Test-Path "$FFmpegPath\bin\ffmpeg.exe")) {
    Write-Host "FFmpeg not found. Downloading automatically..." -ForegroundColor Yellow

    $ffmpegUrl = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip"
    $zipPath = "$env:TEMP\ffmpeg.zip"
    $extractPath = "$PSScriptRoot\third_party"

    # Download FFmpeg ZIP
    Write-Host "Downloading FFmpeg from $ffmpegUrl..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $ffmpegUrl -OutFile $zipPath

    # Extract ZIP
    Write-Host "Extracting FFmpeg..." -ForegroundColor Yellow
    Expand-Archive -Path $zipPath -DestinationPath $extractPath -Force

    # Find the folder inside and move it to $FFmpegPath
    $extracted = Get-ChildItem -Directory -Path $extractPath | Where-Object { $_.Name -like "ffmpeg*" } | Select-Object -First 1
    if ($extracted) {
        Move-Item "$extractPath\$($extracted.Name)" "$FFmpegPath" -Force
    } else {
        Write-Host "ERROR: Failed to extract FFmpeg." -ForegroundColor Red
        exit 1
    }

    Remove-Item $zipPath -Force
    Write-Host "✅ FFmpeg downloaded and extracted successfully to $FFmpegPath" -ForegroundColor Green
}

# Verify FFmpeg structure
$requiredPaths = @(
    "$FFmpegPath\include\libavcodec\avcodec.h",
    "$FFmpegPath\lib\avcodec.lib",
    "$FFmpegPath\bin"
)

foreach ($path in $requiredPaths) {
    if (-not (Test-Path -Path $path)) {
        Write-Host "ERROR: Required FFmpeg component not found: $path" -ForegroundColor Red
        Write-Host "Please ensure you downloaded the 'dev' package and extracted it properly." -ForegroundColor Yellow
        exit 1
    }
}

Write-Host "FFmpeg found at: $FFmpegPath" -ForegroundColor Green

# Check if the build directory exists, if not, configure CMake
if (-not (Test-Path -Path ".\build")) {
    Write-Host "Build directory not found. Configuring CMake..." -ForegroundColor Yellow
    cmake -S . -B build -DFFMPEG_ROOT="$FFmpegPath"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configuration failed."
        exit 1
    }
} else {
    Write-Host "Build directory exists. Reconfiguring with FFmpeg path..." -ForegroundColor Yellow
    cmake -S . -B build -DFFMPEG_ROOT="$FFmpegPath"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake reconfiguration failed."
        exit 1
    }
}

# Build the project
Write-Host "Building the project..." -ForegroundColor Yellow
cmake --build build --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake build failed."
    Write-Host ""
    Write-Host "Common issues:" -ForegroundColor Yellow
    Write-Host "- Make sure you have Visual Studio or Visual Studio Build Tools installed" -ForegroundColor Cyan
    Write-Host "- Verify FFmpeg libraries are for the correct architecture (x64/x86)" -ForegroundColor Cyan
    exit 1
}

# Copy FFmpeg DLLs manually if the automatic copy failed
$dllSource = "$FFmpegPath\bin"
$dllTarget = ".\build\Release"

if (Test-Path -Path $dllTarget) {
    Write-Host "Copying FFmpeg DLLs..." -ForegroundColor Yellow
    
    $ffmpegDlls = Get-ChildItem -Path $dllSource -Filter "*.dll" | Where-Object { 
        $_.Name -match "^(avcodec|avformat|avutil|swscale|swresample)" 
    }
    
    foreach ($dll in $ffmpegDlls) {
        $targetPath = Join-Path $dllTarget $dll.Name
        if (-not (Test-Path -Path $targetPath)) {
            Copy-Item -Path $dll.FullName -Destination $targetPath -Force
            Write-Host "  Copied: $($dll.Name)" -ForegroundColor Cyan
        }
    }
}

# Check if executable was created
$exePath = ".\build\Release\VideoEditor.exe"
if (-not (Test-Path -Path $exePath)) {
    Write-Host "ERROR: VideoEditor.exe was not created!" -ForegroundColor Red
    Write-Host "Check the build output above for errors." -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "Build completed successfully!" -ForegroundColor Green
Write-Host "Running the Video Editor..." -ForegroundColor Yellow

# Run the application
try {
    Start-Process -FilePath $exePath -WorkingDirectory (Split-Path $exePath -Parent)
    Write-Host ""
    Write-Host "Video Editor started!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Usage Instructions:" -ForegroundColor Yellow
    Write-Host "- Click 'Open Video' to load a video file" -ForegroundColor Cyan
    Write-Host "- Use Play/Pause/Stop buttons to control playback" -ForegroundColor Cyan
    Write-Host "- Use the slider to seek through the video" -ForegroundColor Cyan
    Write-Host "- Supported formats: MP4, AVI, MOV, MKV, WMV, FLV, WebM, M4V, 3GP" -ForegroundColor Cyan
} catch {
    Write-Host "ERROR: Failed to start the application: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host ""
    Write-Host "Try running the executable directly:" -ForegroundColor Yellow
    Write-Host "$exePath" -ForegroundColor Cyan
}