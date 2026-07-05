param(
  [Parameter(Mandatory = $true)]
  [string]$Esp8266,

  [Parameter(Mandatory = $true)]
  [string]$Esp32,

  [ValidateSet('Smoke', 'Full')]
  [string]$Mode = 'Full'
)

$ErrorActionPreference = 'Stop'

Write-Host "[AI] Running $Mode validation for ESP8266=$Esp8266 ESP32=$Esp32"

pwsh ./scripts/tests/Run-SmokeTests.ps1 -Esp8266 $Esp8266 -Esp32 $Esp32 -Mode $Mode
if ($LASTEXITCODE -ne 0)
{
  Write-Error "[AI] Validation failed with exit code $LASTEXITCODE"
  exit $LASTEXITCODE
}

Write-Host "[AI] Validation completed successfully"
