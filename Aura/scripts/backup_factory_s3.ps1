param(
  [string]$Port = "COM3",
  [string]$Chip = "esp32s3",
  [int]$Baud = 460800,
  [int]$SizeBytes = 0x800000,
  [string]$BaseDir = "B:\ESP32-Tab\backups"
)

$ErrorActionPreference = "Stop"

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$outDir = Join-Path $BaseDir "factory-s3-$ts"
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

$esptool = "C:\Users\pathe\.platformio\packages\tool-esptoolpy\esptool.py"
$python = "b:/ESP32-Tab/.venv/Scripts/python.exe"
$outFile = Join-Path $outDir "factory-full-8mb.bin"

Write-Host "Backing up from $Port to $outFile"
& $python $esptool --chip $Chip --port $Port --baud $Baud read_flash 0x0 $SizeBytes $outFile

$hash = Get-FileHash $outFile -Algorithm SHA256
Get-Item $outFile | Select-Object FullName, Length, LastWriteTime
Write-Host "SHA256: $($hash.Hash)"
Write-Host "Done."
