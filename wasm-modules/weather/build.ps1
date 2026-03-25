param(
  [string]$Clang = "clang",
  [string]$Source = "weather.c",
  [string]$Output = "out/weather-v1.wasm"
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command $Clang -ErrorAction SilentlyContinue)) {
  Write-Error "clang not found. Install LLVM clang or pass -Clang with full path."
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

$args = @(
  "--target=wasm32",
  "-Oz",
  "-nostdlib",
  "-fno-builtin",
  "-fno-stack-protector",
  "-fvisibility=hidden",
  "-Wl,--no-entry",
  "-Wl,--strip-all",
  "-Wl,--export=module_abi_required",
  "-Wl,--export=on_init",
  "-Wl,--export=render",
  "-Wl,--allow-undefined",
  "-o", $Output,
  $Source
)

& $Clang @args

if ($LASTEXITCODE -ne 0) {
  throw "build failed"
}

Write-Host "Built $Output"
