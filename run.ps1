param(
    [string]$FFmpegPath = "C:\Program Files\ffmpeg"
)

Write-Host "Video Editor Build Script" -ForegroundColor Green
Write-Host "=========================" -ForegroundColor Green

# ==================== EXPERIMENTAL CODE ====================

function Add-ToPath {
    param($dir)
    if (Test-Path $dir -and -not ($env:Path -split ';' | Where-Object { $_ -eq $dir })) {
        Write-Host "Añadiendo '$dir' a PATH de esta sesión..." -ForegroundColor Cyan
        $env:Path = "$env:Path;$dir"
    }
}

# --- 1) Verificar CMake ---
$cmakeExe = "$env:ProgramFiles\CMake\bin\cmake.exe"
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    if (Test-Path $cmakeExe) {
        # Instalado pero no en PATH
        Add-ToPath (Split-Path $cmakeExe)
        Write-Host "✅ CMake detectado e integrado en PATH." -ForegroundColor Green
    } else {
        Write-Host "`n❌ CMake no está instalado." -ForegroundColor Red
        Write-Host "¿Lo instalo con winget? (Requiere admin)" -ForegroundColor Yellow
        $resp = Read-Host "[s/n]"
        if ($resp -eq "s") {
            Write-Host "Instalando CMake vía winget, espera un momento…" -ForegroundColor Cyan
            # -e = exact match, --silent = sin prompts
            Start-Process winget -ArgumentList 'install','Kitware.CMake','-e','--silent' -Verb RunAs -Wait
            # Tras instalación, integramos a PATH
            Add-ToPath (Split-Path $cmakeExe)
            if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
                Write-Host "❌ No se encontró CMake tras la instalación. Por favor reinicia la consola." -ForegroundColor Red
                exit 1
            }
            Write-Host "✅ CMake instalado y listo en esta sesión." -ForegroundColor Green
        } else {
            Write-Host "Por favor instala CMake manualmente desde https://cmake.org/download/" -ForegroundColor Cyan
            exit 1
        }
    }
} else {
    Write-Host "✅ CMake disponible: $(Get-Command cmake).Source" -ForegroundColor Green
}

# --- 2) Verificar MSBuild (VS o Build Tools) ---
function Find-MSBuild {
    $vsPaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($p in $vsPaths) {
        if (Test-Path $p) { return $p }
    }
    # Buscar en registros de carpetas
    $found = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022" -Recurse -Filter "MSBuild.exe" -ErrorAction SilentlyContinue
    if ($found) { return $found[0].FullName }
    return $null
}

$msbuildPath = Find-MSBuild
if (-not $msbuildPath) {
    Write-Host "`n❌ MSBuild no encontrado." -ForegroundColor Red
    Write-Host "¿Instalo Visual Studio Build Tools con winget? (Requiere admin)" -ForegroundColor Yellow
    $resp = Read-Host "[s/n]"
    if ($resp -eq "s") {
        Write-Host "Instalando Build Tools vía winget, espera un momento…" -ForegroundColor Cyan
        Start-Process winget -ArgumentList 'install','Microsoft.VisualStudio.2022.BuildTools','-e','--silent' -Verb RunAs -Wait
        # Rebuscar ahora
        $msbuildPath = Find-MSBuild
        if (-not $msbuildPath) {
            Write-Host "❌ Aún no se encuentra MSBuild. Reinicia la consola o instala manualmente." -ForegroundColor Red
            exit 1
        }
        Write-Host "✅ MSBuild localizado en: $msbuildPath" -ForegroundColor Green
    } else {
        Write-Host "Descarga desde: https://visualstudio.microsoft.com/visual-cpp-build-tools/" -ForegroundColor Cyan
        exit 1
    }
} else {
    Write-Host "✅ MSBuild encontrado en: $msbuildPath" -ForegroundColor Green
}

# ==================== FIN DEL EXPERIMENTAL CODE ====================

# Define default FFmpeg path
if (-not $FFmpegPath) {
    $FFmpegPath = "$PSScriptRoot\third_party\ffmpeg"
}

# Auto-download FFmpeg if not found (ONLY WORKS FOR WINDOWS)
if (-not (Test-Path "$FFmpegPath\bin\ffmpeg.exe")) {
    Write-Host "FFmpeg not found. Downloading automatically..." -ForegroundColor Yellow

    $ffmpegUrl   = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip"
    $zipPath     = "$env:TEMP\ffmpeg.zip"
    $extractPath = "$PSScriptRoot\third_party"

    Write-Host "Downloading FFmpeg from $ffmpegUrl..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $ffmpegUrl -OutFile $zipPath

    Write-Host "Extracting FFmpeg..." -ForegroundColor Yellow
    Expand-Archive -Path $zipPath -DestinationPath $extractPath -Force

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

# Configurar o reconfigurar CMake
if (-not (Test-Path -Path ".\build")) {
    Write-Host "Build directory not found. Configuring CMake..." -ForegroundColor Yellow
    cmake -S . -B build -DFFMPEG_ROOT="$FFmpegPath"
    if ($LASTEXITCODE -ne 0) { Write-Error "CMake configuration failed."; exit 1 }
} else {
    Write-Host "Build directory exists. Reconfiguring with FFmpeg path..." -ForegroundColor Yellow
    cmake -S . -B build -DFFMPEG_ROOT="$FFmpegPath"
    if ($LASTEXITCODE -ne 0) { Write-Error "CMake reconfiguration failed."; exit 1 }
}

# Build the project
Write-Host "Building the project..." -ForegroundColor Yellow
cmake --build build --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake build failed."
    Write-Host ""
    Write-Host "Common issues:" -ForegroundColor Yellow
    Write-Host "- Asegúrate de tener Visual Studio o Build Tools instalados" -ForegroundColor Cyan
    Write-Host "- Verifica que las librerías de FFmpeg sean x64 si tu build es x64" -ForegroundColor Cyan
    exit 1
}

# Copiar DLLs de FFmpeg si faltan
$dllSource = "$FFmpegPath\bin"
$dllTarget = ".\build\Release"
if (Test-Path -Path $dllTarget) {
    Write-Host "Copying FFmpeg DLLs..." -ForegroundColor Yellow
    $ffmpegDlls = Get-ChildItem -Path $dllSource -Filter "*.dll" |
                  Where-Object { $_.Name -match "^(avcodec|avformat|avutil|swscale|swresample)" }
    foreach ($dll in $ffmpegDlls) {
        $dest = Join-Path $dllTarget $dll.Name
        if (-not (Test-Path $dest)) {
            Copy-Item -Path $dll.FullName -Destination $dest -Force
            Write-Host "  Copied: $($dll.Name)" -ForegroundColor Cyan
        }
    }

    $ffmpegExe = Join-Path $dllSource 'ffmpeg.exe'
    if (Test-Path $ffmpegExe) {
        $destExe = Join-Path $dllTarget 'ffmpeg.exe'
        if (-not (Test-Path $destExe)) {
            Copy-Item -Path $ffmpegExe -Destination $destExe -Force
            Write-Host "  Copied: ffmpeg.exe" -ForegroundColor Cyan
        }
    }
}

# Ejecutable
$exePath = ".\build\Release\VideoEditor.exe"
if (-not (Test-Path -Path $exePath)) {
    Write-Host "ERROR: VideoEditor.exe no fue creado!" -ForegroundColor Red
    exit 1
}

Write-Host "" ; Write-Host "Build completed successfully!" -ForegroundColor Green
Write-Host "Running the Video Editor..." -ForegroundColor Yellow
Start-Process -FilePath $exePath -WorkingDirectory (Split-Path $exePath -Parent)
