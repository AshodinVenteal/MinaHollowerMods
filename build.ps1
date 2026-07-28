[CmdletBinding()]
param(
    [ValidateSet("CodeOnly", "Release")]
    [string]$Mode = "CodeOnly",
    [Parameter(Mandatory)]
    [string]$ApiRoot,
    [string]$PayloadDataDirectory,
    [string]$OriginalAnb,
    [string]$Layout,
    [string]$FramesDirectory,
    [string]$OutputRoot,
    [string]$Version = "dev"
)

$ErrorActionPreference = "Stop"
$repositoryRoot = $PSScriptRoot
$xmarkRoot = Join-Path $repositoryRoot "mods\x-mark-burn"
$clashrendRoot = Join-Path $repositoryRoot "mods\clashrend-claymore"

function Resolve-RequiredPath {
    param(
        [string]$Path,
        [string]$Name,
        [ValidateSet("File", "Directory")]
        [string]$Kind
    )

    if (-not $Path) {
        throw "$Name is required for a release build."
    }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $item = Get-Item -LiteralPath $resolved
    if ($Kind -eq "File" -and $item.PSIsContainer) {
        throw "$Name must be a file: $resolved"
    }
    if ($Kind -eq "Directory" -and -not $item.PSIsContainer) {
        throw "$Name must be a directory: $resolved"
    }
    return $resolved
}

function Reset-BuildDirectory {
    param(
        [string]$Path,
        [string]$AllowedRoot
    )

    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd("\")
    $fullRoot = [IO.Path]::GetFullPath($AllowedRoot).TrimEnd("\")
    if (-not $fullPath.StartsWith("$fullRoot\", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to reset a directory outside the output root: $fullPath"
    }
    if (Test-Path -LiteralPath $fullPath) {
        Remove-Item -LiteralPath $fullPath -Recurse -Force
    }
    New-Item -ItemType Directory -Path $fullPath -Force | Out-Null
    return $fullPath
}

if ($Version -notmatch "^[A-Za-z0-9][A-Za-z0-9._-]*$") {
    throw "Version may contain only letters, numbers, dots, underscores, and hyphens."
}

$api = Resolve-RequiredPath -Path $ApiRoot -Name "ApiRoot" -Kind Directory

& (Join-Path $xmarkRoot "build.ps1") -ApiRoot $api -Configuration Release
if ($LASTEXITCODE -ne 0) {
    throw "X Mark Burn compilation failed with exit code $LASTEXITCODE"
}

$dll = Join-Path $xmarkRoot "build\mod.dll"
if (-not (Test-Path -LiteralPath $dll)) {
    throw "Compiler completed without producing $dll"
}

if ($Mode -eq "CodeOnly") {
    [pscustomobject]@{
        Mode = $Mode
        Dll = $dll
        Bytes = (Get-Item -LiteralPath $dll).Length
    }
    return
}

$payloadData = Resolve-RequiredPath -Path $PayloadDataDirectory -Name "PayloadDataDirectory" -Kind Directory
$original = Resolve-RequiredPath -Path $OriginalAnb -Name "OriginalAnb" -Kind File
$layoutPath = Resolve-RequiredPath -Path $Layout -Name "Layout" -Kind File
$frames = Resolve-RequiredPath -Path $FramesDirectory -Name "FramesDirectory" -Kind Directory

if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repositoryRoot "out"
}
$output = [IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $output -Force | Out-Null

$releaseName = "ClashrendClaymore-$Version"
$releaseRoot = Reset-BuildDirectory `
    -Path (Join-Path $output "staging\$releaseName") `
    -AllowedRoot $output
$modRoot = Join-Path $releaseRoot "payload\mods\ClashrendClaymore"
New-Item -ItemType Directory -Path $modRoot -Force | Out-Null

Copy-Item -LiteralPath $payloadData -Destination $modRoot -Recurse -Force
Copy-Item -LiteralPath $dll -Destination (Join-Path $modRoot "mod.dll") -Force
Copy-Item -LiteralPath (Join-Path $clashrendRoot "mod.yc") `
    -Destination (Join-Path $modRoot "mod.yc") -Force

$packagedAnb = Join-Path $modRoot "data\player\hammer.anb.yc"
& (Join-Path $clashrendRoot "package-sprites.ps1") `
    -OriginalAnb $original `
    -Layout $layoutPath `
    -FramesDirectory $frames `
    -OutputAnb $packagedAnb
if ($LASTEXITCODE -ne 0) {
    throw "Clashrend sprite packaging failed with exit code $LASTEXITCODE"
}

$requiredFiles = @(
    (Join-Path $modRoot "mod.dll"),
    (Join-Path $modRoot "mod.yc"),
    $packagedAnb
)
foreach ($required in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Release payload is missing $required"
    }
}

$manifest = Get-Content -LiteralPath (Join-Path $modRoot "mod.yc") -Raw
if ($manifest -notmatch 'id:\s*"clashrendclaymore"') {
    throw "Release manifest does not identify Clashrend Claymore."
}

$magic = [IO.File]::ReadAllBytes($packagedAnb)[0..3]
if ([Text.Encoding]::ASCII.GetString($magic) -ne "YCD`0") {
    throw "Packaged hammer ANB has invalid YCD magic."
}

$diagnostics = @(
    ([IO.Path]::ChangeExtension($packagedAnb, ".review.png")),
    "$packagedAnb.build_report.json"
)
foreach ($diagnostic in $diagnostics) {
    if (Test-Path -LiteralPath $diagnostic) {
        Remove-Item -LiteralPath $diagnostic -Force
    }
}

$archive = Join-Path $output "$releaseName.zip"
& (Join-Path $repositoryRoot "scripts\package-release.ps1") `
    -ReleaseDirectory $releaseRoot `
    -ArchivePath $archive

[pscustomobject]@{
    Mode = $Mode
    Release = $releaseRoot
    Archive = $archive
    ArchiveSha256 = "$archive.sha256"
    PayloadFiles = (Get-ChildItem -LiteralPath $modRoot -Recurse -File).Count
}
