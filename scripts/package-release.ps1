param(
    [Parameter(Mandatory)]
    [string]$ReleaseDirectory,
    [Parameter(Mandatory)]
    [string]$ArchivePath
)

$ErrorActionPreference = "Stop"
$release = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$archive = [IO.Path]::GetFullPath($ArchivePath)
$checksumPath = Join-Path $release "SHA256SUMS.txt"

$checksumLines = Get-ChildItem -LiteralPath $release -Recurse -File |
    Where-Object { $_.FullName -ne $checksumPath } |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($release.Length + 1).Replace("\", "/")
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $relative"
    }

[IO.File]::WriteAllLines(
    $checksumPath,
    $checksumLines,
    [Text.UTF8Encoding]::new($false))

$archiveParent = Split-Path -Parent $archive
New-Item -ItemType Directory -Path $archiveParent -Force | Out-Null
$temporary = Join-Path $archiveParent (
    ".{0}-{1}.tmp" -f [IO.Path]::GetFileName($archive), [guid]::NewGuid().ToString("N"))

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::Open(
    $temporary,
    [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem -LiteralPath $release -Recurse -File | Sort-Object FullName) {
        $entryName = $file.FullName.Substring($release.Length + 1).Replace("\", "/")
        $entry = $zip.CreateEntry($entryName, [IO.Compression.CompressionLevel]::Optimal)
        $input = [IO.File]::OpenRead($file.FullName)
        $output = $entry.Open()
        try {
            $input.CopyTo($output)
        } finally {
            $output.Dispose()
            $input.Dispose()
        }
    }
} finally {
    $zip.Dispose()
}

Move-Item -LiteralPath $temporary -Destination $archive -Force

$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(
    "$archive.sha256",
    "$archiveHash  $([IO.Path]::GetFileName($archive))`r`n",
    [Text.UTF8Encoding]::new($false))

$expected = @{}
foreach ($line in $checksumLines) {
    if ($line -notmatch "^([0-9a-f]{64})  (.+)$") {
        throw "Invalid checksum line: $line"
    }
    $expected[$Matches[2]] = $Matches[1]
}

$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        foreach ($entry in $zip.Entries) {
            $entryPath = $entry.FullName.Replace("\", "/")
            if ($entryPath -eq "SHA256SUMS.txt" -or $entryPath.EndsWith("/")) {
                continue
            }
            if (-not $expected.ContainsKey($entryPath)) {
                throw "Archive contains an unchecked entry: $entryPath"
            }
            $stream = $entry.Open()
            try {
                $actual = [BitConverter]::ToString(
                    $sha256.ComputeHash($stream)).Replace("-", "").ToLowerInvariant()
            } finally {
                $stream.Dispose()
            }
            if ($actual -ne $expected[$entryPath]) {
                throw "Checksum mismatch in archive: $entryPath"
            }
            $expected.Remove($entryPath)
        }
    } finally {
        $sha256.Dispose()
    }
} finally {
    $zip.Dispose()
}

if ($expected.Count -ne 0) {
    throw "Archive is missing $($expected.Count) checksummed file(s)."
}

[pscustomobject]@{
    Archive = $archive
    Sha256 = $archiveHash
    Bytes = (Get-Item -LiteralPath $archive).Length
    Files = $checksumLines.Count
    Validated = $true
}

