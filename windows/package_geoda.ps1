# GeoDa Packaging Script
# Package Release directory into ZIP file for distribution

param(
    [string]$OutputPath = "e:\stuintern",
    [string]$Version = "1.0.0"
)

$ReleaseDir = "e:\stuintern\geo-da_-dev_2026\BuildTools\windows\Release"
$ZipFileName = "GeoDa_v${Version}_Win64.zip"
$ZipFilePath = Join-Path $OutputPath $ZipFileName

Write-Host "=== GeoDa Packaging Script ===" -ForegroundColor Cyan
Write-Host "Source: $ReleaseDir"
Write-Host "Output: $ZipFilePath"

# Check source directory
if (-not (Test-Path $ReleaseDir)) {
    Write-Host "ERROR: Release directory not found!" -ForegroundColor Red
    exit 1
}

# Check required files
$RequiredFiles = @(
    "GeoDa.exe",
    "gdal.dll",
    "proj_9.dll",
    "libopenblas.dll"
)

Write-Host "`nChecking required files..." -ForegroundColor Yellow
$AllFilesPresent = $true
foreach ($file in $RequiredFiles) {
    $filePath = Join-Path $ReleaseDir $file
    if (Test-Path $filePath) {
        Write-Host "  [OK] $file" -ForegroundColor Green
    } else {
        Write-Host "  [MISSING] $file" -ForegroundColor Red
        $AllFilesPresent = $false
    }
}

if (-not $AllFilesPresent) {
    Write-Host "ERROR: Missing required files!" -ForegroundColor Red
    exit 1
}

# Calculate size
$TotalSize = (Get-ChildItem $ReleaseDir -Recurse | Measure-Object -Property Length -Sum).Sum
Write-Host ("`nRelease directory size: {0:N2} MB" -f ($TotalSize / 1MB)) -ForegroundColor Cyan

# Create ZIP
Write-Host "`nCreating ZIP file..." -ForegroundColor Yellow

if (Test-Path $ZipFilePath) {
    Remove-Item $ZipFilePath -Force
    Write-Host "Removed existing ZIP file"
}

# Use .NET compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($ReleaseDir, $ZipFilePath, [System.IO.Compression.CompressionLevel]::Optimal, $false)

# Show result
$ZipSize = (Get-Item $ZipFilePath).Length
Write-Host "`n=== Packaging Complete ===" -ForegroundColor Green
Write-Host ("ZIP file: {0}" -f $ZipFilePath)
Write-Host ("ZIP size: {0:N2} MB" -f ($ZipSize / 1MB))
Write-Host ("Compression ratio: {0:P1}" -f ($ZipSize / $TotalSize))

Write-Host "`nNOTES:" -ForegroundColor Yellow
Write-Host "1. Test on another PC before distribution"
Write-Host "2. Recipient needs Windows 10/11 64-bit"
Write-Host "3. Extract ZIP and run GeoDa.exe"