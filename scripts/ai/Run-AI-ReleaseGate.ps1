param(
  [Parameter(Mandatory = $true)]
  [string]$Esp8266,

  [Parameter(Mandatory = $true)]
  [string]$Esp32,

  [switch]$UploadFirmware,
  [switch]$UploadFilesystem,
  [switch]$SkipBuild,

  [int]$SoakIterations = 5,
  [int]$PauseSeconds = 1
)

$ErrorActionPreference = 'Stop'

function Invoke-Step {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Name,

    [Parameter(Mandatory = $true)]
    [scriptblock]$Action
  )

  Write-Host "[AI] Step: $Name"
  & $Action
  if ($LASTEXITCODE -ne 0)
  {
    Write-Error "[AI] Step failed: $Name (exit=$LASTEXITCODE)"
    exit $LASTEXITCODE
  }
}

if (-not $SkipBuild)
{
  Invoke-Step -Name 'Build esp8266_serial' -Action { platformio run -e esp8266_serial }
  Invoke-Step -Name 'Build esp32_serial' -Action { platformio run -e esp32_serial }
}

if ($UploadFirmware)
{
  Invoke-Step -Name 'Upload ESP8266 firmware' -Action { platformio run -e esp8266_serial -t upload }
  Invoke-Step -Name 'Upload ESP32 firmware' -Action { platformio run -e esp32_serial -t upload }
}

if ($UploadFilesystem)
{
  Invoke-Step -Name 'Upload ESP8266 filesystem' -Action { platformio run -e esp8266_serial -t uploadfs }
  Invoke-Step -Name 'Upload ESP32 filesystem' -Action { platformio run -e esp32_serial -t uploadfs }
}

Invoke-Step -Name 'Full functional validation' -Action {
  pwsh ./scripts/tests/Run-SmokeTests.ps1 -Esp8266 $Esp8266 -Esp32 $Esp32 -Mode Full
}

Invoke-Step -Name 'Full soak validation' -Action {
  pwsh ./scripts/tests/Run-SoakTests.ps1 -Esp8266 $Esp8266 -Esp32 $Esp32 -Mode Full -Iterations $SoakIterations -PauseSeconds $PauseSeconds -OutPath scripts/tests/soak-report-full.json
}

Write-Host '[AI] Release gate completed successfully'
