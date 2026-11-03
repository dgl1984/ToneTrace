<#
.SYNOPSIS
Verifies that a release-notes file is valid, unmangled UTF-8 and, optionally,
that the published GitHub release body matches it byte-for-byte in text.

.DESCRIPTION
The 1.1.0 release body was corrupted twice: a UTF-8 BOM was prepended and the
em dash bytes were decoded through an OEM code page, producing visible mojibake.
This script fails on those conditions before a release is published.

.PARAMETER Path
Release-notes file. Defaults to RELEASE_NOTES.md beside the repository root.

.PARAMETER Version
Expected semantic version. Defaults to the trimmed contents of VERSION.

.PARAMETER Tag
Optional published tag to compare, for example v1.2.0. Requires GitHub CLI.

.PARAMETER Repo
Optional owner/name for the comparison. Defaults to the current gh repository.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_release_notes.ps1

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_release_notes.ps1 -Tag v1.2.0
#>
[CmdletBinding()]
param(
    [string]$Path,
    [string]$Version,
    [string]$Tag,
    [string]$Repo
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Path) { $Path = Join-Path $root 'RELEASE_NOTES.md' }
if (-not (Test-Path -LiteralPath $Path)) { throw "Release notes not found: $Path" }

if (-not $Version) {
    $versionPath = Join-Path $root 'VERSION'
    if (-not (Test-Path -LiteralPath $versionPath)) { throw 'VERSION not found' }
    $Version = (Get-Content -LiteralPath $versionPath -Raw).Trim()
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Expected version is not semantic: $Version"
}

$bytes = [IO.File]::ReadAllBytes($Path)
if ($bytes.Length -eq 0) { throw "Release notes are empty: $Path" }
if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
    throw 'Release notes start with a UTF-8 BOM. Save them as UTF-8 without BOM.'
}

$strictUtf8 = New-Object System.Text.UTF8Encoding($false, $true)
try {
    $text = $strictUtf8.GetString($bytes)
}
catch {
    throw 'Release notes are not valid UTF-8. Re-save the file as UTF-8 without BOM.'
}

if ($text.IndexOf([char]0xFFFD) -ge 0) {
    throw 'Release notes contain U+FFFD replacement characters; text was lost in an encoding conversion.'
}
if ($text.IndexOf([char]0) -ge 0) {
    throw 'Release notes contain NUL bytes; the file is not plain UTF-8 text.'
}

# Mojibake signatures seen when UTF-8 punctuation is decoded through a code
# page. Built from code points so this script itself stays ASCII and Windows
# PowerShell 5.1 can parse it without a BOM.
$mojibake = @(
    ([string][char]0x0047 + [char]0x00C7),                  # G + C-cedilla
    ([string][char]0x00C3),                                 # A-tilde prefix
    ([string][char]0x00C2),                                 # A-circumflex prefix
    ([string][char]0x00E2 + [char]0x20AC)                   # a-circumflex + euro
)
foreach ($marker in $mojibake) {
    if ($text.Contains($marker)) {
        throw "Release notes contain a mojibake marker. Regenerate the file as UTF-8."
    }
}

$firstLine = ($text -split "`r?`n")[0]
$publicVersion = (($Version -split '\.') | Select-Object -First 2) -join '.'
if ($firstLine -notlike "*$Version*" -and $firstLine -notlike "*$publicVersion*") {
    throw "Release notes heading does not name version ${Version} or public version ${publicVersion}: $firstLine"
}
if (-not $text.EndsWith("`n")) {
    throw 'Release notes must end with a newline.'
}
if (($text -split "`r?`n").Count -lt 5) {
    throw 'Release notes look too short to be a release description.'
}

Write-Host "Release notes encoding and version checks passed: $Path"

function Normalize([string]$value) {
    $lines = $value -split "`r?`n"
    for ($i = 0; $i -lt $lines.Count; $i++) { $lines[$i] = $lines[$i].TrimEnd() }
    return (($lines -join "`n").Trim())
}

if ($Tag) {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        throw 'GitHub CLI (gh) is required for published-body verification.'
    }

    $arguments = @('release', 'view', $Tag, '--json', 'body', '--jq', '.body')
    if ($Repo) { $arguments += @('--repo', $Repo) }

    # Windows PowerShell 5.1 decodes native output with the console code page.
    # Force UTF-8 so the comparison sees the published bytes, not a re-encoding.
    $previousOutputEncoding = $OutputEncoding
    $previousConsoleEncoding = [Console]::OutputEncoding
    try {
        $OutputEncoding = New-Object System.Text.UTF8Encoding($false)
        [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
        $published = & gh @arguments
        if ($LASTEXITCODE -ne 0) { throw "gh release view failed for $Tag" }
    }
    finally {
        $OutputEncoding = $previousOutputEncoding
        [Console]::OutputEncoding = $previousConsoleEncoding
    }

    $publishedText = ($published | Out-String)
    if ((Normalize $publishedText) -ne (Normalize $text)) {
        throw "Published release body for $Tag does not match $Path. Recreate the release with --notes-file."
    }
    Write-Host "Published release body matches $Path for $Tag"
}
