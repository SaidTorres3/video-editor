param(
    [string]$FFmpegPath = "C:\Program Files\ffmpeg",
    [switch]$Static
)

# Se recomienda instalar FFmpeg con:
# vcpkg install ffmpeg[dav1d,openh264,x264,x265,mp3lame,fdk-aac,opus,zlib]:x64-windows-static
# Si piden build estática y no cambiaron la ruta por defecto,
# intentamos adivinar la ubicación de vcpkg estático.
if ($Static -and $FFmpegPath -eq "C:\Program Files\ffmpeg") {
    $guess = "C:\tools\vcpkg\installed\x64-windows-static"
    if (Test-Path "$guess\lib\avcodec.lib") {
        Write-Host "Modo estático detectado. Usando FFmpeg desde $guess" -ForegroundColor Cyan
        $FFmpegPath = $guess
    } else {
        Write-Host "Modo estático solicitado pero no se encontró FFmpeg en $guess." -ForegroundColor Yellow
    }
}

# Validar que la instalación es realmente estática
if ($Static) {
    if (-not (Test-Path "$FFmpegPath\lib\avcodec.lib")) {
        Write-Host "ERROR: No se encontró una instalación estática de FFmpeg en $FFmpegPath" -ForegroundColor Red
        Write-Host "Instala 'ffmpeg[dav1d,openh264,x264,x265,mp3lame,fdk-aac,opus,zlib]:x64-windows-static' con vcpkg o especifica la ruta con -FFmpegPath" -ForegroundColor Yellow
        exit 1
    }
    $dlls = Get-ChildItem "$FFmpegPath\bin" -Filter "avcodec*.dll" -ErrorAction SilentlyContinue
    if ($dlls) {
        Write-Host "ERROR: La ruta $FFmpegPath contiene DLLs de FFmpeg. Se requiere la variante estática." -ForegroundColor Red
        Write-Host "Asegúrate de haber instalado la tripleta x64-windows-static" -ForegroundColor Yellow
        exit 1
    }
}

Write-Host "Video Editor Build Script" -ForegroundColor Green
Write-Host "=========================" -ForegroundColor Green

function Add-ToPath {
    param($dir)
    if (Test-Path $dir -and -not ($env:Path -split ';' | Where-Object { $_ -eq $dir })) {
        Write-Host "Añadiendo '$dir' a PATH de esta sesión..." -ForegroundColor Cyan
        $env:Path = "$env:Path;$dir"
    }
}

# 1) Verificar CMake
$cmakeExe = "$env:ProgramFiles\CMake\bin\cmake.exe"
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    if (Test-Path $cmakeExe) {
        Add-ToPath (Split-Path $cmakeExe)
        Write-Host "✅ CMake detectado e integrado en PATH." -ForegroundColor Green
    } else {
        Write-Host "`n❌ CMake no está instalado." -ForegroundColor Red
        Write-Host "¿Lo instalo con winget? (Requiere admin) [s/n]" -ForegroundColor Yellow
        if ((Read-Host) -eq "s") {
            Start-Process winget -ArgumentList 'install','Kitware.CMake','-e','--silent' -Verb RunAs -Wait
            Add-ToPath (Split-Path $cmakeExe)
            if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
                Write-Host "❌ No se encontró CMake tras la instalación. Reinicia la consola." -ForegroundColor Red
                exit 1
            }
            Write-Host "✅ CMake instalado y listo." -ForegroundColor Green
        } else {
            Write-Host "Por favor instala CMake manualmente: https://cmake.org/download/" -ForegroundColor Cyan
            exit 1
        }
    }
} else {
    Write-Host "✅ CMake disponible: $(Get-Command cmake).Source" -ForegroundColor Green
}

# 2) Verificar MSBuild
function Find-MSBuild {
    $vsPaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($p in $vsPaths) { if (Test-Path $p) { return $p } }
    $found = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022" -Recurse -Filter "MSBuild.exe" -ErrorAction SilentlyContinue
    return $found ? $found[0].FullName : $null
}

$msbuildPath = Find-MSBuild
if (-not $msbuildPath) {
    Write-Host "`n❌ MSBuild no encontrado." -ForegroundColor Red
    Write-Host "¿Instalo Build Tools con winget? (Requiere admin) [s/n]" -ForegroundColor Yellow
    if ((Read-Host) -eq "s") {
        Start-Process winget -ArgumentList 'install','Microsoft.VisualStudio.2022.BuildTools','-e','--silent' -Verb RunAs -Wait
        $msbuildPath = Find-MSBuild
        if (-not $msbuildPath) {
            Write-Host "❌ Aún no se encuentra MSBuild. Reinicia o instala manualmente." -ForegroundColor Red
            exit 1
        }
        Write-Host "✅ MSBuild encontrado en: $msbuildPath" -ForegroundColor Green
    } else {
        Write-Host "Descarga desde: https://visualstudio.microsoft.com/visual-cpp-build-tools/" -ForegroundColor Cyan
        exit 1
    }
} else {
    Write-Host "✅ MSBuild encontrado en: $msbuildPath" -ForegroundColor Green
}

# Ruta por defecto si no se especificó
if (-not $FFmpegPath) {
    $FFmpegPath = "$PSScriptRoot\third_party\ffmpeg"
}

# 3) Descarga automática solo en modo compartido
if (-not $Static.IsPresent) {
    if (-not (Test-Path "$FFmpegPath\bin\ffmpeg.exe")) {
        Write-Host "FFmpeg no encontrado en bin/. Descargando..." -ForegroundColor Yellow
        $ffmpegUrl   = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip"
        $zipPath     = "$env:TEMP\ffmpeg.zip"
        $extractPath = "$PSScriptRoot\third_party"

        Invoke-WebRequest -Uri $ffmpegUrl -OutFile $zipPath
        Expand-Archive -Path $zipPath -DestinationPath $extractPath -Force

        $extracted = Get-ChildItem -Directory -Path $extractPath | Where-Object Name -Like "ffmpeg*" | Select-Object -First 1
        if ($extracted) {
            Move-Item "$extractPath\$($extracted.Name)" "$FFmpegPath" -Force
            Remove-Item $zipPath -Force
            Write-Host "✅ FFmpeg descargado y extraído en $FFmpegPath" -ForegroundColor Green
        } else {
            Write-Host "❌ Error al extraer FFmpeg." -ForegroundColor Red
            exit 1
        }
    }
}

# 4) Verificar componentes de FFmpeg según modo
if ($Static.IsPresent) {
    $required = @(
        "$FFmpegPath\include\libavcodec\avcodec.h",
        "$FFmpegPath\lib\avcodec.lib"
    )
} else {
    $required = @(
        "$FFmpegPath\include\libavcodec\avcodec.h",
        "$FFmpegPath\lib\avcodec.lib",
        "$FFmpegPath\bin\ffmpeg.exe"
    )
}

foreach ($p in $required) {
    if (-not (Test-Path $p)) {
        Write-Host "ERROR: No se encontró FFmpeg en: $p" -ForegroundColor Red
        Write-Host "Asegúrate de descargar el paquete 'dev' y extraerlo correctamente." -ForegroundColor Yellow
        exit 1
    }
}
Write-Host "FFmpeg validado en: $FFmpegPath" -ForegroundColor Green

# 5) Configurar/reconfigurar CMake
$staticFlag = $Static.IsPresent ? "ON" : "OFF"
if (-not (Test-Path ".\build")) {
    Write-Host "Configurando CMake..." -ForegroundColor Yellow
    cmake -S . -B build -DFFMPEG_ROOT="$FFmpegPath" -DUSE_STATIC_FFMPEG=$staticFlag
} else {
    Write-Host "Reconfigurando CMake con nuevos parámetros..." -ForegroundColor Yellow
    cmake -S . -B build -DFFMPEG_ROOT="$FFmpegPath" -DUSE_STATIC_FFMPEG=$staticFlag
}
if ($LASTEXITCODE -ne 0) { Write-Error "Fallo la configuración de CMake."; exit 1 }

# 6) Compilar el proyecto
Write-Host "Compilando proyecto..." -ForegroundColor Yellow
cmake --build build --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Error "Fallo la compilación con CMake."
    exit 1
}

# 7) Copiar DLLs solo en modo compartido
if (-not $Static.IsPresent) {
    $dllSrc = "$FFmpegPath\bin"
    $dllDst = ".\build\Release"
    if (Test-Path $dllDst) {
        Write-Host "Copiando DLLs de FFmpeg..." -ForegroundColor Yellow
        Get-ChildItem $dllSrc -Filter "*.dll" |
          Where-Object { $_.Name -match "^(avcodec|avformat|avutil|swscale|swresample)" } |
          ForEach-Object {
              Copy-Item $_.FullName -Destination $dllDst -Force
              Write-Host "  Copiado: $($_.Name)" -ForegroundColor Cyan
          }
    }
}

# 8) Ejecutable final
$exe = ".\build\Release\VideoEditor.exe"
if (-not (Test-Path $exe)) {
    Write-Host "ERROR: ¡VideoEditor.exe no fue creado!" -ForegroundColor Red
    exit 1
}

Write-Host "`n✅ Build completada. Ejecutando Video Editor..." -ForegroundColor Green
Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe -Parent)