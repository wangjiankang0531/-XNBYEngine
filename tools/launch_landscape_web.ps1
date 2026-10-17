param([int]$Port = 8766, [switch]$NoBrowser)
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskUrl = "http://127.0.0.1:$Port"
$taskActive = $false
try {
  $taskHealth = Invoke-RestMethod -Uri "$taskUrl/api/health" -TimeoutSec 2
  $taskActive = $taskHealth.application -eq 'hongtu.landscape.web.v1'
} catch {}
if (-not $taskActive) {
  $taskPython = Join-Path $taskRoot 'runtime/python/python.exe'
  if (-not (Test-Path -LiteralPath $taskPython)) { throw 'Packaged Python runtime is missing.' }
  $taskServer = Join-Path $taskRoot 'tools/landscape_web_server.py'
  $taskLogs = Join-Path $taskRoot 'work/web_logs'
  New-Item -ItemType Directory -Path $taskLogs -Force | Out-Null
  $taskStamp = Get-Date -Format 'yyyyMMdd_HHmmss'
  $taskProcess = Start-Process -FilePath $taskPython -ArgumentList @('"' + $taskServer + '"', '--port=' + $Port) -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskLogs "$taskStamp.out.log") -RedirectStandardError (Join-Path $taskLogs "$taskStamp.err.log")
  $taskProcess.Id | Set-Content -LiteralPath (Join-Path $taskLogs "$taskStamp.pid")
  for ($taskAttempt = 0; $taskAttempt -lt 20; $taskAttempt++) {
    Start-Sleep -Milliseconds 250
    try {
      $taskHealth = Invoke-RestMethod -Uri "$taskUrl/api/health" -TimeoutSec 1
      if ($taskHealth.application -eq 'hongtu.landscape.web.v1') { $taskActive = $true; break }
    } catch {}
    if ($taskProcess.HasExited) { break }
  }
  if (-not $taskActive) { throw "Viewer failed to start; inspect $taskLogs" }
}
Write-Output "HongTu landscape viewer: $taskUrl"
if (-not $NoBrowser) { Start-Process $taskUrl }
