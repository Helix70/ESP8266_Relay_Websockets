param(
  [string]$Esp8266 = '192.168.2.154',
  [string]$Esp32 = '192.168.2.195',
  [ValidateSet('Smoke', 'Full')]
  [string]$Mode = 'Smoke',
  [int]$Iterations = 20,
  [int]$PauseSeconds = 2,
  [string]$OutPath = 'scripts/tests/soak-report.json'
)

$ErrorActionPreference = 'Stop'

$harness = Join-Path $PSScriptRoot 'Invoke-BoardFunctionalHarness.ps1'
$lastReport = Join-Path $PSScriptRoot 'last-functional-report.json'
$targets = @($Esp8266, $Esp32)

$startedAt = Get-Date
$runs = New-Object System.Collections.Generic.List[object]

for ($i = 1; $i -le $Iterations; $i++) {
  Write-Host "[Soak] Iteration $i/$Iterations"

  & $harness -Targets $targets -Mode $Mode -ReportPath $lastReport
  $exitCode = $LASTEXITCODE

  $reportObj = $null
  if (Test-Path $lastReport) {
    try { $reportObj = Get-Content -Raw $lastReport | ConvertFrom-Json } catch { }
  }

  $runs.Add([pscustomobject]@{
      iteration = $i
      exitCode  = $exitCode
      summary   = if ($reportObj) { $reportObj.summary } else { $null }
      failed    = if ($reportObj) { @($reportObj.tests | Where-Object { -not $_.passed }) } else { @() }
      coverage  = if ($reportObj) { $reportObj.coverage } else { @() }
      timestamp = Get-Date
    })

  if ($i -lt $Iterations -and $PauseSeconds -gt 0) {
    Start-Sleep -Seconds $PauseSeconds
  }
}

$finishedAt = Get-Date

$totalTests = 0
$totalFailures = 0
foreach ($run in $runs) {
  if ($run.summary) {
    $totalTests += [int]$run.summary.total
    $totalFailures += [int]$run.summary.failed
  }
}

$flakeRate = 0.0
if ($totalTests -gt 0) {
  $flakeRate = [math]::Round(($totalFailures / $totalTests) * 100.0, 2)
}

$soakReport = [pscustomobject]@{
  startedAt      = $startedAt
  finishedAt     = $finishedAt
  durationSec    = [math]::Round((New-TimeSpan -Start $startedAt -End $finishedAt).TotalSeconds, 2)
  mode           = $Mode
  iterations     = $Iterations
  targets        = $targets
  aggregate      = [pscustomobject]@{
    totalTests    = $totalTests
    totalFailures = $totalFailures
    flakeRatePct  = $flakeRate
  }
  runs           = $runs
}

$outDir = Split-Path -Path $OutPath -Parent
if ($outDir -and -not (Test-Path $outDir)) {
  New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

$soakReport | ConvertTo-Json -Depth 12 | Set-Content -Path $OutPath -Encoding UTF8

Write-Host "Soak complete. Iterations=$Iterations FlakeRate=${flakeRate}% Report=$OutPath"

if ($totalFailures -gt 0) {
  exit 1
}

exit 0
