param(
    [Parameter(Mandatory)]
    [string]$ApiRoot,
    [switch]$SkipAssets
)

$ErrorActionPreference = "Stop"
$modRoot = $PSScriptRoot
$repositoryRoot = Resolve-Path (Join-Path $modRoot "..\..")
$api = Resolve-Path $ApiRoot

. (Join-Path $repositoryRoot "scripts\msvc-env.ps1")

if (-not $SkipAssets) {
    $generators = @(
        "build_annex_assets.py",
        "build_teleport_palette.py",
        "build_hub_entrance.py",
        "patch_startup_menu_binary.py"
    )
    foreach ($generator in $generators) {
        & python (Join-Path $modRoot "tools\$generator")
        if ($LASTEXITCODE -ne 0) {
            throw "$generator failed with exit code $LASTEXITCODE"
        }
    }
}

$output = Join-Path $modRoot "build"
New-Item -ItemType Directory -Path $output -Force | Out-Null

cl /nologo /std:c++17 /EHsc /W4 /O2 /LD `
    /I"$api" `
    (Join-Path $modRoot "src\mod.cpp") `
    /Fo"$output\mod.obj" `
    /Fe"$output\mod.dll"
if ($LASTEXITCODE -ne 0) {
    throw "C++ build failed with exit code $LASTEXITCODE"
}

Write-Host "Built $output\mod.dll"
