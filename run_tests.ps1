# run_tests.ps1 — Build and run VideoEditorTests
#
# Usage:
#   pwsh .\run_tests.ps1          # Dynamic build (default)
#   pwsh .\run_tests.ps1 -Static  # Static build

param(
    [switch]$Static,
    [switch]$BuildOnly
)

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Video Editor Test Runner" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

$scriptDir = $PSScriptRoot
$buildDir = Join-Path $scriptDir "build"
$ffmpegRoot = Join-Path $scriptDir "third_party\ffmpeg"

# 1. Ensure build directory exists and is configured
if (-not (Test-Path $buildDir)) {
    Write-Host "[1/3] Configuring CMake..." -ForegroundColor Yellow
    $staticFlag = if ($Static) { "ON" } else { "OFF" }
    
    $curlRoot = Join-Path $scriptDir "vendor\libcurl"
    if ($Static) {
        $guess = "C:\tools\vcpkg\installed\x64-windows-static"
        if (Test-Path "$guess\lib\avcodec.lib") {
            $ffmpegRoot = $guess
        }
        $curlRoot = $ffmpegRoot
    }

    & cmake -S "$scriptDir" -B "$buildDir" `
        "-DUSE_STATIC_FFMPEG:BOOL=$staticFlag" `
        "-DFFMPEG_ROOT=$ffmpegRoot" `
        "-DCURL_ROOT=$curlRoot" `
        "-DBUILD_TESTS=ON"

    if ($LASTEXITCODE -ne 0) {
        Write-Host "CMake configuration failed!" -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "[1/3] Build directory exists, reconfiguring..." -ForegroundColor Yellow
    $staticFlag = if ($Static) { "ON" } else { "OFF" }
    
    $curlRoot = Join-Path $scriptDir "vendor\libcurl"
    if ($Static) {
        $guess = "C:\tools\vcpkg\installed\x64-windows-static"
        if (Test-Path "$guess\lib\avcodec.lib") {
            $ffmpegRoot = $guess
        }
        $curlRoot = $ffmpegRoot
    }

    & cmake -S "$scriptDir" -B "$buildDir" `
        "-DUSE_STATIC_FFMPEG:BOOL=$staticFlag" `
        "-DFFMPEG_ROOT=$ffmpegRoot" `
        "-DCURL_ROOT=$curlRoot" `
        "-DBUILD_TESTS=ON"
}

# 2. Build the test target
Write-Host "[2/3] Building VideoEditorTests..." -ForegroundColor Yellow
& cmake --build "$buildDir" --config Release --target VideoEditorTests
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Build FAILED!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Build successful." -ForegroundColor Green

if ($BuildOnly) {
    Write-Host "Build-only mode, skipping test execution." -ForegroundColor Yellow
    exit 0
}

# 3. Run the tests
$testExe = Join-Path $buildDir "Release\VideoEditorTests.exe"
if (-not (Test-Path $testExe)) {
    Write-Host "Test executable not found at: $testExe" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "[3/3] Running tests..." -ForegroundColor Yellow
Write-Host "----------------------------------------" -ForegroundColor DarkGray
Write-Host ""

& "$testExe"
$exitCode = $LASTEXITCODE

Write-Host ""
Write-Host "----------------------------------------" -ForegroundColor DarkGray

if ($exitCode -eq 0) {
    Write-Host ""
    Write-Host "All tests PASSED!" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "Some tests FAILED (exit code: $exitCode)" -ForegroundColor Red
}

exit $exitCode
