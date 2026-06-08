Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$scriptPath = Join-Path $PSScriptRoot "build-release-notes.ps1"

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Needle,
        [Parameter(Mandatory = $true)][string]$Message
    )

    if (-not $Text.Contains($Needle)) {
        throw $Message
    }
}

function Assert-NotContains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Needle,
        [Parameter(Mandatory = $true)][string]$Message
    )

    if ($Text.Contains($Needle)) {
        throw $Message
    }
}

function New-TestWorkspace {
    $root = Join-Path ([IO.Path]::GetTempPath()) ("asnap-release-notes-test-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $root | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $root "dist") | Out-Null
    Set-Content -Path (Join-Path $root "dist\ASnap-Setup-windows-x64-0.1.9.exe") -Value "fake installer" -Encoding utf8
    Set-Content -Path (Join-Path $root "dist\ASnap-Setup-windows-x64-0.1.9.sha256.txt") -Value "abc123  ASnap-Setup-windows-x64-0.1.9.exe" -Encoding utf8
    return $root
}

function Test-BuildsNotesFromMatchingChangelogSection {
    $root = New-TestWorkspace
    try {
        $changelog = Join-Path $root "CHANGELOG.md"
        @"
# Changelog

## [Unreleased]

- Future work that must not appear.

## [0.1.9] - 2026-06-08

- Upgrade CI to Qt 6.8.3.
- Generate release notes from changelog.

## [0.1.8] - 2026-04-24

- Old release note that must not appear.
"@ | Set-Content -Path $changelog -Encoding utf8

        $outputPath = Join-Path $root "dist\release-notes.md"
        & $scriptPath -Version "0.1.9" -DistDir (Join-Path $root "dist") -ChangelogPath $changelog -OutputPath $outputPath

        $notes = Get-Content -Path $outputPath -Raw
        Assert-Contains $notes "ASnap-Setup-windows-x64-0.1.9.exe" "Installer name was not included."
        Assert-Contains $notes "abc123" "SHA256 value was not included."
        Assert-Contains $notes "Upgrade CI to Qt 6.8.3." "Matching changelog section was not included."
        Assert-Contains $notes "Generate release notes from changelog." "Second matching changelog item was not included."
        Assert-NotContains $notes "Future work" "Unreleased section leaked into release notes."
        Assert-NotContains $notes "Old release note" "Previous release section leaked into release notes."
    }
    finally {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}

function Test-MissingChangelogSectionFailsClearly {
    $root = New-TestWorkspace
    try {
        $changelog = Join-Path $root "CHANGELOG.md"
        @"
# Changelog

## [0.1.8] - 2026-04-24

- Old release.
"@ | Set-Content -Path $changelog -Encoding utf8

        $failed = $false
        try {
            & $scriptPath -Version "0.1.9" -DistDir (Join-Path $root "dist") -ChangelogPath $changelog -OutputPath (Join-Path $root "dist\release-notes.md")
        }
        catch {
            $failed = $true
            Assert-Contains $_.Exception.Message "CHANGELOG.md section for version 0.1.9 was not found" "Failure message did not explain the missing section."
        }

        if (-not $failed) {
            throw "Expected missing changelog section to fail."
        }
    }
    finally {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}

function Test-PreservesUtf8ChangelogTextUnderWindowsPowerShell {
    $root = New-TestWorkspace
    try {
        $changelog = Join-Path $root "CHANGELOG.md"
        $changelogText = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("IyBDaGFuZ2Vsb2cKCiMjIFswLjEuOV0gLSAyMDI2LTA2LTA4CgotIOWPkeW4g+a1geeoi+aUueS4uuS7jiBgQ0hBTkdFTE9HLm1kYCDnlJ/miJDmm7TmlrDor7TmmI7jgIIKLSDmnKzlnLDmiZPljIXohJrmnKzlkIzmraXliLAgUXQgNi44LjPjgIIK"))
        $expectedFirstNote = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("5Y+R5biD5rWB56iL5pS55Li6"))
        $expectedSecondNote = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("5pys5Zyw5omT5YyF6ISa5pys"))
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [IO.File]::WriteAllText($changelog, $changelogText, $utf8NoBom)

        $outputPath = Join-Path $root "dist\release-notes.md"
        & $scriptPath -Version "0.1.9" -DistDir (Join-Path $root "dist") -ChangelogPath $changelog -OutputPath $outputPath

        $notes = Get-Content -Path $outputPath -Raw -Encoding utf8
        Assert-Contains $notes $expectedFirstNote "UTF-8 Chinese changelog text was not preserved."
        Assert-Contains $notes $expectedSecondNote "Second UTF-8 Chinese changelog item was not preserved."
    }
    finally {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}

Test-BuildsNotesFromMatchingChangelogSection
Test-MissingChangelogSectionFailsClearly
Test-PreservesUtf8ChangelogTextUnderWindowsPowerShell
Write-Host "release notes tests passed"
