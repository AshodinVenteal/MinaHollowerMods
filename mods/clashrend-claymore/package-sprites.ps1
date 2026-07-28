param(
    [Parameter(Mandatory)]
    [string]$OriginalAnb,
    [Parameter(Mandatory)]
    [string]$Layout,
    [Parameter(Mandatory)]
    [string]$FramesDirectory,
    [Parameter(Mandatory)]
    [string]$OutputAnb,
    [string]$Manifest
)

$ErrorActionPreference = "Stop"
$tools = Join-Path $PSScriptRoot "tools"
$frames = (Resolve-Path -LiteralPath $FramesDirectory).Path
$manifestPath = if ($Manifest) {
    (Resolve-Path -LiteralPath $Manifest).Path
} else {
    Join-Path $frames "manifest.json"
}

if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Frame manifest not found: $manifestPath"
}

$output = if ([IO.Path]::IsPathRooted($OutputAnb)) {
    [IO.Path]::GetFullPath($OutputAnb)
} else {
    [IO.Path]::GetFullPath((Join-Path (Get-Location).Path $OutputAnb))
}
$reviewSheet = [IO.Path]::ChangeExtension($output, ".review.png")

& python (Join-Path $tools "clashrend_frame_workflow.py") compose `
    --frames-dir $frames `
    --layout (Resolve-Path -LiteralPath $Layout).Path `
    --out-sheet $reviewSheet `
    --prefix clashrend `
    --no-preview
if ($LASTEXITCODE -ne 0) {
    throw "Frame composition failed with exit code $LASTEXITCODE"
}

& python (Join-Path $tools "build_clashrend_hammer_anb.py") `
    --original-anb (Resolve-Path -LiteralPath $OriginalAnb).Path `
    --layout (Resolve-Path -LiteralPath $Layout).Path `
    --manifest $manifestPath `
    --frames-dir $frames `
    --out $output `
    --preserve-slots
if ($LASTEXITCODE -ne 0) {
    throw "ANB packaging failed with exit code $LASTEXITCODE"
}

Write-Host "Packaged $output"
Write-Host "Review sheet: $reviewSheet"
Write-Host "Build report: $output.build_report.json"
