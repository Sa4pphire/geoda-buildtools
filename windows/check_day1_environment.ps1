[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$checks = [System.Collections.Generic.List[object]]::new()

function Add-Check {
    param(
        [string]$Name,
        [bool]$Passed,
        [string]$Details,
        [bool]$Required = $true
    )

    $checks.Add([pscustomobject]@{
        Name = $Name
        Status = if ($Passed) { 'PASS' } elseif ($Required) { 'FAIL' } else { 'WARN' }
        Details = $Details
        Required = $Required
    })
}

function Find-MSBuild {
    $onPath = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($onPath) {
        return $onPath.Source
    }

    $vswhereCandidates = @(
        'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe',
        'C:\Program Files\Microsoft Visual Studio\Installer\vswhere.exe'
    )
    foreach ($candidate in $vswhereCandidates) {
        if (Test-Path -LiteralPath $candidate) {
            $found = & $candidate -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' 2>$null | Select-Object -First 1
            if ($found) {
                return $found
            }
        }
    }

    $patterns = @(
        'C:\Program Files\Microsoft Visual Studio\2022\*\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files (x86)\Microsoft Visual Studio\2019\*\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\18\*\MSBuild\Current\Bin\MSBuild.exe'
    )
    foreach ($pattern in $patterns) {
        $found = Get-Item -Path $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) {
            return $found.FullName
        }
    }

    return $null
}

$code = Get-Command code.cmd -ErrorAction SilentlyContinue
if (-not $code) {
    $code = Get-Command code.exe -ErrorAction SilentlyContinue
}
Add-Check 'Visual Studio Code' ([bool]$code) $(if ($code) { $code.Source } else { 'code command not found' })

$profilePath = if ($env:USERPROFILE) { $env:USERPROFILE } else { [Environment]::GetFolderPath('UserProfile') }
$extensionRoot = Join-Path $profilePath '.vscode\extensions'
$cppExtension = Get-ChildItem -Path $extensionRoot -Directory -Filter 'ms-vscode.cpptools-*' -ErrorAction SilentlyContinue | Select-Object -First 1
Add-Check 'VS Code Microsoft C/C++ extension' ([bool]$cppExtension) $(if ($cppExtension) { $cppExtension.Name } else { 'Install ms-vscode.cpptools' })

$msbuild = Find-MSBuild
Add-Check 'MSBuild and MSVC toolchain' ([bool]$msbuild) $(if ($msbuild) { $msbuild } else { 'Not found. VS Code alone cannot compile this MSVC project.' })

$requiredPaths = [ordered]@{
    'VS solution' = 'BuildTools\windows\GeoDa.vs2019.sln'
    'GeoDa project' = 'BuildTools\windows\GeoDa.vs2019.vcxproj'
    'GDALRasterIO project' = 'BuildTools\GDALRasterIO\GDALRasterIO.vcxproj'
    'Dependency headers' = 'BuildTools\vcpkg_libs\include'
    'Dependency libraries' = 'BuildTools\vcpkg_libs\lib'
    'Runtime DLLs' = 'BuildTools\vcpkg_libs\bin'
    'GDAL data' = 'data\gdal'
    'PROJ database' = 'data\proj\proj.db'
    'GeoDa launcher' = 'BuildTools\windows\Release\run_geoda.bat'
}
foreach ($item in $requiredPaths.GetEnumerator()) {
    $fullPath = Join-Path $repoRoot $item.Value
    Add-Check $item.Key (Test-Path -LiteralPath $fullPath) $item.Value
}

$exePath = Join-Path $repoRoot 'BuildTools\windows\Release\GeoDa.exe'
Add-Check 'Release executable' (Test-Path -LiteralPath $exePath) 'BuildTools\windows\Release\GeoDa.exe (created after a successful build)' $false

$bundledLibs = Join-Path $repoRoot 'BuildTools\vcpkg_libs\lib'
$toolsetMarkers = @()
if (Test-Path -LiteralPath $bundledLibs) {
    $toolsetMarkers = @(Get-ChildItem -LiteralPath $bundledLibs -Filter '*.lib' -File |
        ForEach-Object {
            if ($_.Name -match 'vc(?<toolset>\d{3})') { "vc$($Matches.toolset)" }
        } |
        Sort-Object -Unique)
}
$toolsetDetails = if ($toolsetMarkers.Count -gt 0) { $toolsetMarkers -join ', ' } else { 'No compiler marker found in library names' }
Add-Check 'Bundled library toolset marker' ($toolsetMarkers.Count -gt 0) $toolsetDetails $false

Write-Host ''
Write-Host 'GeoDa Day 1 environment check'
Write-Host "Repository: $repoRoot"
Write-Host ''
$checks | Format-Table -AutoSize Name, Status, Details

$failures = @($checks | Where-Object { $_.Required -and $_.Status -eq 'FAIL' })
if ($failures.Count -gt 0) {
    Write-Host "Environment is not ready: $($failures.Count) required check(s) failed." -ForegroundColor Red
    exit 1
}

Write-Host 'Environment is ready for a Release x64 build.' -ForegroundColor Green
exit 0
