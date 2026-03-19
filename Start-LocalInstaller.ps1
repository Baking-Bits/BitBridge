$ErrorActionPreference = "Stop"

$port = 8080
$prefix = "http://localhost:$port/"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

Add-Type -AssemblyName System.Web

$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add($prefix)
$listener.Start()

Write-Host "Local installer running at $prefix"
Write-Host "Press Ctrl+C to stop."

Start-Process $prefix | Out-Null

function Get-MimeType {
  param([string]$path)

  switch ([System.IO.Path]::GetExtension($path).ToLowerInvariant()) {
    ".html" { "text/html; charset=utf-8"; break }
    ".css"  { "text/css; charset=utf-8"; break }
    ".js"   { "application/javascript; charset=utf-8"; break }
    ".json" { "application/json; charset=utf-8"; break }
    ".bin"  { "application/octet-stream"; break }
    ".png"  { "image/png"; break }
    ".jpg"  { "image/jpeg"; break }
    ".jpeg" { "image/jpeg"; break }
    ".svg"  { "image/svg+xml"; break }
    ".txt"  { "text/plain; charset=utf-8"; break }
    default  { "application/octet-stream" }
  }
}

function Write-JsonResponse {
  param(
    [Parameter(Mandatory=$true)]$context,
    [Parameter(Mandatory=$true)]$payload,
    [int]$statusCode = 200
  )

  $json = $payload | ConvertTo-Json -Depth 8
  $bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
  $context.Response.StatusCode = $statusCode
  $context.Response.ContentType = "application/json; charset=utf-8"
  $context.Response.ContentLength64 = $bytes.LongLength
  $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
  $context.Response.OutputStream.Close()
}

function Handle-BackupCurrentImage {
  param(
    [Parameter(Mandatory=$true)]$context,
    [Parameter(Mandatory=$true)][string]$root
  )

  $body = ""
  $reader = New-Object System.IO.StreamReader($context.Request.InputStream, $context.Request.ContentEncoding)
  $body = $reader.ReadToEnd()
  $reader.Close()

  $port = "COM3"
  $chip = "esp32s3"
  $baud = 460800
  $sizeHex = "0x800000"

  if (-not [string]::IsNullOrWhiteSpace($body)) {
    try {
      $req = $body | ConvertFrom-Json
      if ($req.port) { $port = [string]$req.port }
      if ($req.chip) { $chip = [string]$req.chip }
      if ($req.baud) { $baud = [int]$req.baud }
      if ($req.sizeHex) { $sizeHex = [string]$req.sizeHex }
    } catch {
      Write-JsonResponse -context $context -payload @{ ok = $false; error = "Invalid JSON body" } -statusCode 400
      return
    }
  }

  $python = Join-Path $root ".venv\Scripts\python.exe"
  $esptool = Join-Path $env:USERPROFILE ".platformio\packages\tool-esptoolpy\esptool.py"
  if (-not (Test-Path $python)) {
    Write-JsonResponse -context $context -payload @{ ok = $false; error = "Python not found at $python" } -statusCode 500
    return
  }
  if (-not (Test-Path $esptool)) {
    Write-JsonResponse -context $context -payload @{ ok = $false; error = "esptool.py not found at $esptool" } -statusCode 500
    return
  }

  $pkgDir = Join-Path $root "packages\factory-s3"
  New-Item -ItemType Directory -Path $pkgDir -Force | Out-Null
  $archiveDir = Join-Path $root "backups\factory-s3"
  New-Item -ItemType Directory -Path $archiveDir -Force | Out-Null

  $imagePath = Join-Path $pkgDir "factory-full-8mb.bin"
  $logPath = Join-Path $pkgDir "backup-last-log.txt"
  $metaPath = Join-Path $pkgDir "backup-metadata.json"

  $stdoutPath = [System.IO.Path]::GetTempFileName()
  $stderrPath = [System.IO.Path]::GetTempFileName()

  try {
    $args = @(
      $esptool,
      "--chip", $chip,
      "--port", $port,
      "--baud", "$baud",
      "read_flash", "0x0", $sizeHex,
      $imagePath
    )

    $proc = Start-Process -FilePath $python -ArgumentList $args -NoNewWindow -PassThru -Wait -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $stdout = ""
    $stderr = ""
    if (Test-Path $stdoutPath) { $stdout = Get-Content -Raw -LiteralPath $stdoutPath }
    if (Test-Path $stderrPath) { $stderr = Get-Content -Raw -LiteralPath $stderrPath }
    $combined = ($stdout + "`n" + $stderr).Trim()
    Set-Content -LiteralPath $logPath -Value $combined -Encoding UTF8

    if ($proc.ExitCode -ne 0 -or -not (Test-Path $imagePath)) {
      Write-JsonResponse -context $context -payload @{
        ok = $false
        error = "Backup failed. Port may be busy or device not in boot mode."
        port = $port
        chip = $chip
        exitCode = $proc.ExitCode
        logPath = "./packages/factory-s3/backup-last-log.txt"
      } -statusCode 500
      return
    }

    $fileInfo = Get-Item -LiteralPath $imagePath
    $hash = Get-FileHash -LiteralPath $imagePath -Algorithm SHA256
    $timestamp = (Get-Date).ToString("yyyyMMdd-HHmmss")
    $archiveImage = Join-Path $archiveDir ("factory-full-8mb-" + $timestamp + ".bin")
    Copy-Item -LiteralPath $imagePath -Destination $archiveImage -Force

    $meta = [ordered]@{
      ok = $true
      port = $port
      chip = $chip
      baud = $baud
      sizeHex = $sizeHex
      file = "factory-full-8mb.bin"
      archiveFile = [System.IO.Path]::GetFileName($archiveImage)
      bytes = $fileInfo.Length
      sha256 = $hash.Hash
      capturedAt = (Get-Date).ToString("o")
      log = "backup-last-log.txt"
    }
    ($meta | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $metaPath -Encoding UTF8

    Write-JsonResponse -context $context -payload @{
      ok = $true
      message = "Factory image saved"
      packagePath = "./packages/factory-s3/"
      manifest = "./packages/factory-s3/manifest.json"
      file = "./packages/factory-s3/factory-full-8mb.bin"
      archiveFile = "./backups/factory-s3/$($meta.archiveFile)"
      bytes = $fileInfo.Length
      sha256 = $hash.Hash
      capturedAt = $meta.capturedAt
    }
  } catch {
    Write-JsonResponse -context $context -payload @{
      ok = $false
      error = $_.Exception.Message
    } -statusCode 500
  } finally {
    if (Test-Path $stdoutPath) { Remove-Item -LiteralPath $stdoutPath -Force -ErrorAction SilentlyContinue }
    if (Test-Path $stderrPath) { Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue }
  }
}

function Handle-BackupStatus {
  param(
    [Parameter(Mandatory=$true)]$context,
    [Parameter(Mandatory=$true)][string]$root
  )

  $pkgDir = Join-Path $root "packages\factory-s3"
  $metaPath = Join-Path $pkgDir "backup-metadata.json"
  $imgPath = Join-Path $pkgDir "factory-full-8mb.bin"

  if ((Test-Path $metaPath) -and (Test-Path $imgPath)) {
    try {
      $meta = Get-Content -Raw -LiteralPath $metaPath | ConvertFrom-Json
      Write-JsonResponse -context $context -payload @{ ok = $true; hasBackup = $true; meta = $meta }
      return
    } catch {}
  }

  Write-JsonResponse -context $context -payload @{ ok = $true; hasBackup = $false }
}

while ($listener.IsListening) {
  $context = $listener.GetContext()
  try {
    $requestPath = [System.Web.HttpUtility]::UrlDecode($context.Request.Url.AbsolutePath)

    if ($requestPath -eq "/api/backup-current" -and $context.Request.HttpMethod -eq "POST") {
      Handle-BackupCurrentImage -context $context -root $root
      continue
    }

    if ($requestPath -eq "/api/backup-status" -and $context.Request.HttpMethod -eq "GET") {
      Handle-BackupStatus -context $context -root $root
      continue
    }

    if ([string]::IsNullOrWhiteSpace($requestPath) -or $requestPath -eq "/") {
      $requestPath = "/index.html"
    }

    $relative = $requestPath.TrimStart('/').Replace('/', [System.IO.Path]::DirectorySeparatorChar)
    $fullPath = Join-Path $root $relative
    $fullPath = [System.IO.Path]::GetFullPath($fullPath)

    if (-not $fullPath.StartsWith([System.IO.Path]::GetFullPath($root), [System.StringComparison]::OrdinalIgnoreCase)) {
      $context.Response.StatusCode = 403
      $context.Response.Close()
      continue
    }

    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
      $context.Response.StatusCode = 404
      $context.Response.Close()
      continue
    }

    $bytes = [System.IO.File]::ReadAllBytes($fullPath)
    $context.Response.StatusCode = 200
    $context.Response.ContentType = Get-MimeType -path $fullPath
    $context.Response.ContentLength64 = $bytes.LongLength
    $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
    $context.Response.OutputStream.Close()
  } catch {
    $context.Response.StatusCode = 500
    $context.Response.Close()
  }
}