param(
  [string]$Esp8266 = '192.168.2.154',
  [string]$Esp32 = '192.168.2.195',
  [ValidateSet('Smoke', 'Full')]
  [string]$Mode = 'Smoke'
)

$targets = @($Esp8266, $Esp32)
$harness = Join-Path $PSScriptRoot 'Invoke-BoardFunctionalHarness.ps1'
$report = Join-Path $PSScriptRoot 'last-functional-report.json'

& $harness -Targets $targets -Mode $Mode -ReportPath $report
exit $LASTEXITCODE
