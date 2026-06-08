param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$DistDir = "dist",
    [string]$ChangelogPath = "CHANGELOG.md",
    [string]$OutputPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Normalize-Version {
    param([Parameter(Mandatory = $true)][string]$Value)

    $normalized = $Value.Trim()
    if ($normalized.StartsWith("v", [StringComparison]::OrdinalIgnoreCase)) {
        $normalized = $normalized.Substring(1)
    }
    if ([string]::IsNullOrWhiteSpace($normalized)) {
        throw "Version must not be empty."
    }
    return $normalized
}

function ConvertFrom-Utf8Base64 {
    param([Parameter(Mandatory = $true)][string]$Value)

    return [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Value))
}

function Get-ChangelogSection {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Version
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "CHANGELOG.md was not found at '$Path'."
    }

    $normalizedVersion = Normalize-Version -Value $Version
    $escapedVersion = [regex]::Escape($normalizedVersion)
    $headingPattern = "^\s*##\s+(?:\[(?:v)?$escapedVersion\]|(?:v)?$escapedVersion)(?:\s|$)"
    $nextHeadingPattern = "^\s*##\s+"
    $lines = Get-Content -LiteralPath $Path -Encoding UTF8
    $insideSection = $false
    $sectionLines = New-Object System.Collections.Generic.List[string]

    foreach ($line in $lines) {
        if (-not $insideSection) {
            if ($line -match $headingPattern) {
                $insideSection = $true
            }
            continue
        }

        if ($line -match $nextHeadingPattern) {
            break
        }

        $sectionLines.Add($line)
    }

    if (-not $insideSection) {
        throw "CHANGELOG.md section for version $normalizedVersion was not found."
    }

    $content = ($sectionLines -join "`n").Trim()
    if ([string]::IsNullOrWhiteSpace($content)) {
        throw "CHANGELOG.md section for version $normalizedVersion is empty."
    }

    return $content
}

function Find-Installer {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Dist directory '$Path' was not found."
    }

    $installer = Get-ChildItem -LiteralPath $Path -Filter "ASnap-Setup-windows-x64-*.exe" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($null -eq $installer) {
        throw "Installer EXE not found in '$Path'."
    }

    return $installer
}

$normalizedVersion = Normalize-Version -Value $Version
$installer = Find-Installer -Path $DistDir
$shaPath = Join-Path $installer.DirectoryName ($installer.BaseName + ".sha256.txt")
if (-not (Test-Path -LiteralPath $shaPath)) {
    throw "Installer SHA256 file not found at '$shaPath'."
}

$shaLine = (Get-Content -LiteralPath $shaPath -Encoding UTF8 | Select-Object -First 1).Trim()
$shaValue = ($shaLine -split "\s+")[0]
if ([string]::IsNullOrWhiteSpace($shaValue)) {
    throw "Installer SHA256 file is empty at '$shaPath'."
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $DistDir "release-notes.md"
}

$changeNotes = Get-ChangelogSection -Path $ChangelogPath -Version $normalizedVersion
$downloadHeading = ConvertFrom-Utf8Base64 -Value "5LiL6L29"
$installerLabel = ConvertFrom-Utf8Base64 -Value "V2luZG93cyDlronoo4XlmajvvJo="
$checksumHeading = ConvertFrom-Utf8Base64 -Value "5qCh6aqM"
$changesHeading = ConvertFrom-Utf8Base64 -Value "5pu05paw"

@"
## $downloadHeading

- ${installerLabel}$($installer.Name)

## $checksumHeading

- SHA256: $shaValue

## $changesHeading

$changeNotes
"@ | Set-Content -LiteralPath $OutputPath -Encoding utf8

Write-Host "Release notes written to $OutputPath"
