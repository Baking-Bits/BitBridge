param(
  [Parameter(Mandatory=$true)]
  [string]$Image,
  [string]$Port = "COM3",
  [string]$Chip = "esp32s3",
  [int]$Baud = 460800
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Image)) {
  throw "Image not found: $Image"
}

$esptool = "C:\Users\pathe\.platformio\packages\tool-esptoolpy\esptool.py"
$python = "b:/ESP32-Tab/.venv/Scripts/python.exe"

Write-Host "Restoring $Image to $Port"
& $python $esptool --chip $Chip --port $Port --baud $Baud erase_flash
& $python $esptool --chip $Chip --port $Port --baud $Baud write_flash 0x0 $Image
Write-Host "Restore complete."
