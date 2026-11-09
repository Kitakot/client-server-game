param(
    [string]$BuildDir = "build",
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    & cmake -S . -B $BuildDir
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed"
    }

    & cmake --build $BuildDir --config $Configuration --target server tests
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }

    & ctest --test-dir $BuildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "Tests failed"
    }
}
finally {
    Pop-Location
}