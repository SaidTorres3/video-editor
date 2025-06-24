# Check if the build directory exists, if not, configure CMake
if (-not (Test-Path -Path ".\build")) {
    Write-Host "Build directory not found. Configuring CMake..."
    cmake -S . -B build
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configuration failed."
        exit 1
    }
}

# Build the project
Write-Host "Building the project..."
cmake --build build
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake build failed."
    exit 1
}

# Run the application
Write-Host "Running the application..."
Start-Process ".\build\Debug\MiVentana.exe"