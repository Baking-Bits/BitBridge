param(
  [string]$ControlPlaneBase = "http://localhost:8787",
  [string]$ModuleVersion = "1.0.0",
  [string]$AppType = "weather"
)

$ErrorActionPreference = "Stop"

Write-Host "1) Build wasm"
./build.ps1

$modulePath = "out/weather-v1.wasm"
if (-not (Test-Path $modulePath)) {
  throw "Missing $modulePath"
}

Write-Host "2) Copy to static /packages/wasm path"
Copy-Item $modulePath "..\..\packages\wasm\weather-v1.wasm" -Force

Write-Host "3) Compute checksum"
$checksum = (Get-FileHash "..\..\packages\wasm\weather-v1.wasm" -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "SHA256: $checksum"

Write-Host "4) Register app module (requires admin session cookie in browser)"
$payload = @{
  appType = $AppType
  version = $ModuleVersion
  moduleUrl = "/packages/wasm/weather-v1.wasm"
  checksumSha256 = $checksum
  minHostAbi = 1
  notes = "Weather Aura module with auto IP geolocation"
} | ConvertTo-Json

Write-Host "Payload:"
Write-Host $payload

Write-Host "Use this in browser devtools console after login:"
Write-Host "fetch('/api/admin/app-modules',{method:'POST',headers:{'Content-Type':'application/json'},credentials:'include',body:JSON.stringify($payload)})"

Write-Host "Done. Then assign appType=weather to a device in admin page."
