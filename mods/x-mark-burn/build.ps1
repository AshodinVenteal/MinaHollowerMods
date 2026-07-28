param(
    [Parameter(Mandatory)]
    [string]$ApiRoot,
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$modRoot = $PSScriptRoot
$repositoryRoot = Resolve-Path (Join-Path $modRoot "..\..")
$api = Resolve-Path $ApiRoot

. (Join-Path $repositoryRoot "scripts\msvc-env.ps1")

$output = Join-Path $modRoot "build"
New-Item -ItemType Directory -Path $output -Force | Out-Null

$flags = @("/nologo", "/std:c++17", "/EHsc", "/W4", "/LD", "/DWIN32_LEAN_AND_MEAN", "/DNOMINMAX")
if ($Configuration -eq "Debug") {
    $flags += @("/Od", "/Zi")
} else {
    $flags += @("/O2", "/Oi", "/GL", "/Gy", "/Gw", "/DNDEBUG")
}

cl @flags `
    /I"$api" `
    /I"$modRoot\src\generated" `
    (Join-Path $modRoot "src\mod.cpp") `
    /Fo"$output\mod.obj" `
    /Fe"$output\mod.dll" `
    /link /nologo /OPT:REF /OPT:ICF user32.lib
if ($LASTEXITCODE -ne 0) {
    throw "C++ build failed with exit code $LASTEXITCODE"
}

Write-Host "Built $output\mod.dll"
