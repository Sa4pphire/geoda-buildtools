[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64')]
    [string]$Platform = 'x64',

    [ValidateRange(1, 64)]
    [int]$MaxCpuCount = 1,

    [switch]$Rebuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

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

function ConvertTo-NativeArgument {
    param(
        [AllowEmptyString()]
        [string]$Argument
    )

    if ($null -eq $Argument -or $Argument.Length -eq 0) {
        return '""'
    }
    if ($Argument -notmatch '[\s"]') {
        return $Argument
    }

    # ProcessStartInfo.ArgumentList is unavailable in Windows PowerShell 5.1.
    # Quote arguments according to the Windows command-line parsing rules.
    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $backslashCount = 0

    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq [char]92) {
            $backslashCount++
            continue
        }

        if ($character -eq [char]34) {
            [void]$builder.Append(('\' * (($backslashCount * 2) + 1)))
            [void]$builder.Append($character)
            $backslashCount = 0
            continue
        }

        if ($backslashCount -gt 0) {
            [void]$builder.Append(('\' * $backslashCount))
            $backslashCount = 0
        }
        [void]$builder.Append($character)
    }

    if ($backslashCount -gt 0) {
        [void]$builder.Append(('\' * ($backslashCount * 2)))
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Invoke-CleanProcess {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [string[]]$ArgumentList,

        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory
    )

    # Some agent/terminal hosts provide both Path and PATH. .NET Framework
    # MSBuild treats child-process environment keys case-insensitively and
    # crashes while launching cl.exe when both spellings are present.
    $sourceEnvironment = [Environment]::GetEnvironmentVariables()
    $cleanEnvironment = [System.Collections.Generic.Dictionary[string, string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase
    )

    foreach ($keyObject in $sourceEnvironment.Keys) {
        $key = [string]$keyObject
        $value = [string]$sourceEnvironment[$keyObject]
        if ($cleanEnvironment.ContainsKey($key)) {
            if ($key -ieq 'Path') {
                $cleanEnvironment[$key] = $cleanEnvironment[$key] + ';' + $value
            }
            continue
        }
        $cleanEnvironment.Add($key, $value)
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.EnvironmentVariables.Clear()

    foreach ($entry in $cleanEnvironment.GetEnumerator()) {
        $environmentName = if ($entry.Key -ieq 'Path') { 'Path' } else { $entry.Key }
        $startInfo.EnvironmentVariables[$environmentName] = $entry.Value
    }
    $startInfo.Arguments = (($ArgumentList | ForEach-Object {
        ConvertTo-NativeArgument -Argument $_
    }) -join ' ')

    $process = [System.Diagnostics.Process]::Start($startInfo)
    $process.WaitForExit()
    return $process.ExitCode
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$solution = Join-Path $PSScriptRoot 'GeoDa.vs2019.sln'
$msbuild = Find-MSBuild

if (-not $msbuild) {
    throw 'MSBuild was not found. Install an MSVC Build Tools workload before building from VS Code.'
}

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
Write-Host "MSBuild: $msbuild"
Write-Host "Target: $target  Configuration: $Configuration  Platform: $Platform"
Write-Host "MSBuild nodes: $MaxCpuCount"

$msbuildArguments = @(
    $solution,
    '/nologo',
    "/m:$MaxCpuCount",
    "/t:$target",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    '/p:PreferredToolArchitecture=x64',
    "/p:CL_MPCount=$MaxCpuCount",
    '/verbosity:minimal'
)
$buildExitCode = Invoke-CleanProcess -FilePath $msbuild -ArgumentList $msbuildArguments -WorkingDirectory $repoRoot

if ($buildExitCode -ne 0) {
    throw "GeoDa build failed with exit code $buildExitCode."
}

$exePath = Join-Path $PSScriptRoot "$Configuration\GeoDa.exe"
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "MSBuild succeeded but the expected executable was not found: $exePath"
}

Write-Host "Build completed: $exePath" -ForegroundColor Green
