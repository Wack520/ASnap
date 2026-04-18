[CmdletBinding()]
param(
    [string]$Configuration = "Release",
    [string]$BuildDir = "build/native",
    [string]$OutputDir = "dist",
    [string]$Version = "",
    [switch]$RunTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    $scriptRoot = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptRoot "..")).Path
}

function Find-CMake {
    $candidates = @(@(
        $env:CMAKE_EXE,
        "C:\Program Files\CMake\bin\cmake.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Users\21115\Qt\Tools\CMake_64\bin\cmake.exe"
    ) | Where-Object { $_ -and (Test-Path $_) })

    if ($candidates.Count -gt 0) {
        return $candidates[0]
    }

    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    throw "cmake.exe was not found. Install CMake or set CMAKE_EXE."
}

function Get-QtRootFromCache {
    param([string]$CachePath)

    if (-not (Test-Path $CachePath)) {
        return $null
    }

    $qtDirLine = Select-String -Path $CachePath -Pattern '^Qt6_DIR:PATH=(.+)$' | Select-Object -First 1
    if ($null -eq $qtDirLine) {
        return $null
    }

    $qtDir = $qtDirLine.Matches[0].Groups[1].Value.Trim()
    if ([string]::IsNullOrWhiteSpace($qtDir)) {
        return $null
    }

    return (Resolve-Path (Join-Path $qtDir '..\..\..')).Path
}

function Find-QtRoot {
    param([string]$RepoRoot, [string]$BuildDirPath)

    $candidates = @(@(
        $env:QT_ROOT_DIR,
        $env:QTDIR,
        (Get-QtRootFromCache -CachePath (Join-Path $BuildDirPath "CMakeCache.txt")),
        "C:\Users\21115\Qt\6.6.3\msvc2019_64",
        "C:\Qt\6.6.3\msvc2019_64"
    ) | Where-Object { $_ -and (Test-Path $_) })

    if ($candidates.Count -gt 0) {
        return (Resolve-Path $candidates[0]).Path
    }

    throw "Qt root was not found. Set QT_ROOT_DIR or configure CMake once first."
}

function Find-WinDeployQt {
    param([string]$QtRoot)

    $candidates = @(@(
        $env:WINDEPLOYQT_EXE,
        (Join-Path $QtRoot "bin\windeployqt.exe"),
        (Join-Path $QtRoot "bin\windeployqt6.exe")
    ) | Where-Object { $_ -and (Test-Path $_) })

    if ($candidates.Count -gt 0) {
        return (Resolve-Path $candidates[0]).Path
    }

    $command = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    throw "windeployqt.exe was not found. Make sure the Qt Widgets toolchain is installed."
}

function Invoke-Step {
    param(
        [string]$Title,
        [scriptblock]$Action
    )

    Write-Host "==> $Title" -ForegroundColor Cyan
    & $Action
}

$repoRoot = Resolve-RepoRoot
$buildDirPath = Join-Path $repoRoot $BuildDir
$outputDirPath = Join-Path $repoRoot $OutputDir
$cmakeExe = Find-CMake
$ctestExe = Join-Path (Split-Path -Parent $cmakeExe) "ctest.exe"
$qtRoot = Find-QtRoot -RepoRoot $repoRoot -BuildDirPath $buildDirPath
$windeployqtExe = Find-WinDeployQt -QtRoot $qtRoot

$versionSuffix = if ([string]::IsNullOrWhiteSpace($Version)) { "local" } else { $Version.Trim() }
$packageFolderName = "ASnap-windows-x64-$versionSuffix"
$packageRoot = Join-Path $outputDirPath $packageFolderName
$zipPath = Join-Path $outputDirPath "$packageFolderName.zip"
$shaPath = Join-Path $outputDirPath "$packageFolderName.sha256.txt"

New-Item -ItemType Directory -Force -Path $buildDirPath | Out-Null
New-Item -ItemType Directory -Force -Path $outputDirPath | Out-Null

Invoke-Step "Configure ($Configuration)" {
    & $cmakeExe -S (Join-Path $repoRoot "native") `
        -B $buildDirPath `
        -G "Visual Studio 17 2022" `
        -A x64 `
        -DCMAKE_PREFIX_PATH="$qtRoot"
}

if ($RunTests.IsPresent) {
    Invoke-Step "Build all targets ($Configuration)" {
        & $cmakeExe --build $buildDirPath --config $Configuration -- /m:1
    }

    Invoke-Step "Run tests ($Configuration)" {
        $env:PATH = (Join-Path $qtRoot "bin") + ";" + $env:PATH
        & $ctestExe --test-dir $buildDirPath -C $Configuration --output-on-failure
    }
} else {
    Invoke-Step "Build app target ($Configuration)" {
        & $cmakeExe --build $buildDirPath --config $Configuration --target ai_screenshot -- /m:1
    }
}

$exeCandidates = @(
    @(
        (Join-Path $buildDirPath "$Configuration\ASnap.exe"),
        (Join-Path $buildDirPath "$Configuration\ai_screenshot.exe")
    ) | Where-Object { Test-Path $_ }
)

if ($exeCandidates.Count -eq 0) {
    throw "Built EXE was not found."
}

$exePath = (Resolve-Path $exeCandidates[0]).Path

if (Test-Path $packageRoot) {
    Remove-Item -Recurse -Force $packageRoot
}
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}
if (Test-Path $shaPath) {
    Remove-Item -Force $shaPath
}

New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
Copy-Item $exePath (Join-Path $packageRoot "ASnap.exe")
Copy-Item (Join-Path $repoRoot "README.md") (Join-Path $packageRoot "README.md")
Copy-Item (Join-Path $repoRoot "LICENSE") (Join-Path $packageRoot "LICENSE")

Invoke-Step "Run windeployqt" {
    & $windeployqtExe `
        --$($Configuration.ToLowerInvariant()) `
        --compiler-runtime `
        --no-translations `
        --verbose 0 `
        (Join-Path $packageRoot "ASnap.exe")
}

Invoke-Step "Create ZIP package" {
    Compress-Archive -Path $packageRoot -DestinationPath $zipPath -CompressionLevel Optimal -Force
}

$hash = Get-FileHash -Algorithm SHA256 -Path $zipPath
"{0} *{1}" -f $hash.Hash.ToLowerInvariant(), (Split-Path $zipPath -Leaf) | Set-Content -Path $shaPath -Encoding ascii

Write-Host ""
Write-Host "Package created:" -ForegroundColor Green
Write-Host "ZIP    : $zipPath"
Write-Host "SHA256 : $shaPath"
