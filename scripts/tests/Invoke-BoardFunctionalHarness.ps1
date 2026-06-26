param(
  [Parameter(Mandatory = $true)]
  [string[]]$Targets,

  [ValidateSet('Smoke', 'Full')]
  [string]$Mode = 'Smoke',

  [int]$TimeoutSec = 20,

  [string]$ReportPath = 'scripts/tests/last-functional-report.json'
)

$ErrorActionPreference = 'Stop'

$results = New-Object System.Collections.Generic.List[object]
$startedAt = Get-Date

# Endpoint catalog used to report route coverage per run.
$endpointCatalog = @(
  [pscustomobject]@{ endpoint = '/'; mode = 'Smoke' },
  [pscustomobject]@{ endpoint = '/relay-config.html'; mode = 'Smoke' },
  [pscustomobject]@{ endpoint = '/boards.html'; mode = 'Smoke' },
  [pscustomobject]@{ endpoint = '/api/templates'; mode = 'Smoke' },
  [pscustomobject]@{ endpoint = '/api/templates/diagnostics'; mode = 'Smoke' },
  [pscustomobject]@{ endpoint = '/api/boards'; mode = 'Smoke' },
  [pscustomobject]@{ endpoint = '/netinfo'; mode = 'Smoke' },
  [pscustomobject]@{ endpoint = '/api/templates (POST create/save)'; mode = 'Smoke' },
  [pscustomobject]@{ endpoint = '/api/templates (POST setactive)'; mode = 'Smoke' },
  [pscustomobject]@{ endpoint = '/api/templates (POST rename)'; mode = 'Smoke' },
  [pscustomobject]@{ endpoint = '/api/templates (POST delete)'; mode = 'Smoke' },
  [pscustomobject]@{ endpoint = '/api/boards (POST save)'; mode = 'Full' },
  [pscustomobject]@{ endpoint = '/api/boards (POST rename)'; mode = 'Full' },
  [pscustomobject]@{ endpoint = '/api/boards (POST setactive)'; mode = 'Full' },
  [pscustomobject]@{ endpoint = '/api/boards (POST delete)'; mode = 'Full' },
  [pscustomobject]@{ endpoint = '/ws'; mode = 'Full' }
)

function Add-Result {
  param(
    [string]$Target,
    [string]$Suite,
    [string]$Test,
    [bool]$Passed,
    [string]$Details = '',
    [string]$Endpoint = ''
  )

  $results.Add([pscustomobject]@{
      target   = $Target
      suite    = $Suite
      test     = $Test
      endpoint = $Endpoint
      passed   = $Passed
      details  = $Details
    })
}

function Normalize-BaseUrl {
  param([string]$Target)

  $value = $Target.Trim()
  if (-not $value.StartsWith('http://') -and -not $value.StartsWith('https://')) {
    $value = 'http://' + $value
  }
  return $value.TrimEnd('/')
}

function Normalize-BoardFilename {
  param([string]$Filename)

  if (-not $Filename) { return '' }
  $name = $Filename
  if ($name.StartsWith('/')) { $name = $name.Substring(1) }
  if ($name.StartsWith('boards/')) { $name = $name.Substring(7) }
  return $name
}

function Normalize-TemplateFilename {
  param([string]$Filename)

  if (-not $Filename) { return '' }
  $name = $Filename.Trim()
  if ($name.StartsWith('/')) { $name = $name.Substring(1) }
  if ($name.StartsWith('templates/')) { $name = $name.Substring(10) }
  return $name
}

function Get-TargetShortId {
  param([string]$Target)

  $raw = ($Target -replace '[^0-9A-Za-z]', '')
  if (-not $raw) { return 'node' }
  if ($raw.Length -le 6) { return $raw.ToLower() }
  return $raw.Substring($raw.Length - 6).ToLower()
}

function Invoke-Api {
  param(
    [string]$Method,
    [string]$Url,
    [hashtable]$Form = $null,
    [int]$TimeoutSec = 20
  )

  $params = @{
    Method             = $Method
    Uri                = $Url
    TimeoutSec         = $TimeoutSec
    SkipHttpErrorCheck = $true
  }

  if ($Form) {
    $params.ContentType = 'application/x-www-form-urlencoded;charset=UTF-8'
    $params.Body = $Form
  }

  $resp = Invoke-WebRequest @params
  $json = $null
  if ($resp.Content) {
    try { $json = $resp.Content | ConvertFrom-Json } catch { }
  }

  return [pscustomobject]@{
    status = [int]$resp.StatusCode
    body   = [string]$resp.Content
    json   = $json
  }
}

function Is-TransientApiFailure {
  param([object]$Response)

  if (-not $Response) { return $true }
  if ($Response.status -eq 0) { return $true }
  if ($Response.status -eq 408 -or $Response.status -eq 429 -or $Response.status -eq 500 -or $Response.status -eq 502 -or $Response.status -eq 503 -or $Response.status -eq 504) {
    return $true
  }

  $body = [string]$Response.body
  if ($body -match 'temp_open_failed|invalid template json|template incompatible or invalid|save failed|template not found|storage busy') {
    return $true
  }

  return $false
}

function Invoke-ApiWithRetry {
  param(
    [string]$Method,
    [string]$Url,
    [hashtable]$Form = $null,
    [int]$TimeoutSec = 20,
    [int]$MaxAttempts = 3,
    [int]$RetryDelayMs = 200
  )

  $last = $null
  for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
    try {
      $last = Invoke-Api -Method $Method -Url $Url -Form $Form -TimeoutSec $TimeoutSec
    } catch {
      $last = [pscustomobject]@{
        status = 0
        body   = $_.Exception.Message
        json   = $null
      }
    }

    if ($last.status -ge 200 -and $last.status -lt 300) {
      return $last
    }

    if (-not (Is-TransientApiFailure -Response $last)) {
      return $last
    }

    if ($attempt -lt $MaxAttempts) {
      Start-Sleep -Milliseconds $RetryDelayMs
    }
  }

  return $last
}

function Test-BasicEndpoints {
  param([string]$Target, [string]$BaseUrl)

  $suite = 'basic'
  $paths = @(
    '/',
    '/relay-config.html',
    '/boards.html',
    '/api/templates',
    '/api/templates/diagnostics',
    '/api/boards',
    '/netinfo'
  )

  foreach ($path in $paths) {
    try {
      $r = Invoke-ApiWithRetry -Method 'GET' -Url ($BaseUrl + $path) -TimeoutSec $TimeoutSec -MaxAttempts 2
      $ok = $r.status -ge 200 -and $r.status -lt 300
      Add-Result -Target $Target -Suite $suite -Test ("GET $path") -Endpoint $path -Passed $ok -Details ("status=" + $r.status)
    } catch {
      Add-Result -Target $Target -Suite $suite -Test ("GET $path") -Endpoint $path -Passed $false -Details $_.Exception.Message
    }
  }
}

function New-TemplateSaveForm {
  param([string]$Title, [int]$RelayCount)

  $form = @{
    title      = $Title
    relayCount = [string]$RelayCount
  }

  for ($i = 1; $i -le $RelayCount; $i++) {
    $form["relay${i}_on"] = "H${i}-ON"
    $form["relay${i}_off"] = "H${i}-OFF"
    $form["relay${i}_mode"] = 'latched'
    $form["relay${i}_group"] = '0'
    $form["relay${i}_pulseTimeout"] = '0'
  }

  return $form
}

function Get-ActiveRelayCount {
  param([object]$BoardsJson)

  $activeFile = Normalize-BoardFilename -Filename ([string]$BoardsJson.activeBoardFile)
  foreach ($b in ($BoardsJson.boards | Where-Object { $_ })) {
    $f = Normalize-BoardFilename -Filename ([string]$b.filename)
    if ($f -eq $activeFile) {
      $rc = [int]$b.relayCount
      if ($rc -eq 16) { return 16 }
      return 8
    }
  }

  return 8
}

function Test-TemplateCrud {
  param([string]$Target, [string]$BaseUrl)

  $suite = 'templates'
  $before = Invoke-ApiWithRetry -Method 'GET' -Url ($BaseUrl + '/api/templates') -TimeoutSec $TimeoutSec
  $boards = Invoke-ApiWithRetry -Method 'GET' -Url ($BaseUrl + '/api/boards') -TimeoutSec $TimeoutSec

  $previousSelected = ''
  $previousSelectedExists = $false
  if ($before.json -and $before.json.selectedTemplate) {
    $previousSelected = Normalize-TemplateFilename -Filename ([string]$before.json.selectedTemplate)
  }
  if ($before.json -and $before.json.templates -and $previousSelected) {
    foreach ($tpl in $before.json.templates) {
      $f = Normalize-TemplateFilename -Filename ([string]$tpl.filename)
      if ($f -eq $previousSelected) {
        $previousSelectedExists = $true
        break
      }
    }
  }

  $relayCount = 8
  if ($boards.json) {
    $relayCount = Get-ActiveRelayCount -BoardsJson $boards.json
  }

  $shortId = Get-TargetShortId -Target $Target
  $stamp = Get-Date -Format 'HHmmss'
  $title = "ht-$shortId-$stamp"
  $form = New-TemplateSaveForm -Title $title -RelayCount $relayCount

  $create = Invoke-ApiWithRetry -Method 'POST' -Url ($BaseUrl + '/api/templates') -Form $form -TimeoutSec $TimeoutSec
  $createOk = ($create.status -eq 200 -and $create.json -and $create.json.ok)
  Add-Result -Target $Target -Suite $suite -Test 'create' -Endpoint '/api/templates (POST create/save)' -Passed $createOk -Details ("status=" + $create.status + ", body=" + $create.body)

  $createdFile = if ($create.json) { Normalize-TemplateFilename -Filename ([string]$create.json.filename) } else { '' }
  if (-not $createOk -or -not $createdFile) {
    return
  }

  Start-Sleep -Milliseconds 150

  $listAfterCreate = Invoke-ApiWithRetry -Method 'GET' -Url ($BaseUrl + '/api/templates') -TimeoutSec $TimeoutSec
  $listContainsCreated = $false
  $fallbackFile = ''
  if ($listAfterCreate.json -and $listAfterCreate.json.templates) {
    foreach ($tpl in $listAfterCreate.json.templates) {
      $listFilename = Normalize-TemplateFilename -Filename ([string]$tpl.filename)
      if ($listFilename -eq $createdFile) {
        $listContainsCreated = $true
      }

      if (-not $fallbackFile) {
        $listTitle = [string]$tpl.title
        if ($listTitle -eq $title) {
          $fallbackFile = $listFilename
        }
      }
    }
  }

  if (-not $listContainsCreated -and $fallbackFile) {
    $createdFile = $fallbackFile
    $listContainsCreated = $true
  }

  Add-Result -Target $Target -Suite $suite -Test 'list contains created' -Endpoint '/api/templates' -Passed $listContainsCreated -Details ("status=" + $listAfterCreate.status + ", requested=" + $createdFile + ", fallback=" + $fallbackFile)

  if (-not $listContainsCreated) {
    return
  }

  $set = Invoke-ApiWithRetry -Method 'POST' -Url ($BaseUrl + '/api/templates') -Form @{ action = 'setactive'; filename = $createdFile } -TimeoutSec $TimeoutSec
  $setOk = ($set.status -eq 200 -and $set.json -and $set.json.ok)
  Add-Result -Target $Target -Suite $suite -Test 'setactive' -Endpoint '/api/templates (POST setactive)' -Passed $setOk -Details ("status=" + $set.status + ", body=" + $set.body)

  $renamedTitle = "$title-r"
  $rename = Invoke-ApiWithRetry -Method 'POST' -Url ($BaseUrl + '/api/templates') -Form @{ action = 'rename'; filename = $createdFile; title = $renamedTitle } -TimeoutSec $TimeoutSec
  $renameOk = ($rename.status -eq 200 -and $rename.json -and $rename.json.ok)
  Add-Result -Target $Target -Suite $suite -Test 'rename' -Endpoint '/api/templates (POST rename)' -Passed $renameOk -Details ("status=" + $rename.status + ", body=" + $rename.body)

  $renameFile = if ($renameOk) { [string]$rename.json.filename } else { $createdFile }
  if (-not $renameFile) { $renameFile = $createdFile }

  $delete = Invoke-ApiWithRetry -Method 'POST' -Url ($BaseUrl + '/api/templates') -Form @{ action = 'delete'; filename = $renameFile } -TimeoutSec $TimeoutSec
  $deleteOk = ($delete.status -eq 200 -and $delete.json -and $delete.json.ok)
  Add-Result -Target $Target -Suite $suite -Test 'delete' -Endpoint '/api/templates (POST delete)' -Passed $deleteOk -Details ("status=" + $delete.status + ", body=" + $delete.body)

  if ($previousSelected -and $previousSelectedExists) {
    $restore = Invoke-ApiWithRetry -Method 'POST' -Url ($BaseUrl + '/api/templates') -Form @{ action = 'setactive'; filename = $previousSelected } -TimeoutSec $TimeoutSec
    $restoreOk = ($restore.status -eq 200 -and $restore.json -and $restore.json.ok)
    Add-Result -Target $Target -Suite $suite -Test 'restore selected' -Endpoint '/api/templates (POST setactive)' -Passed $restoreOk -Details ("status=" + $restore.status + ", body=" + $restore.body)
  }
}

function New-BoardSaveForm {
  param([string]$Title, [int]$RelayCount)

  $form = @{
    action     = 'save'
    title      = $Title
    name       = $Title
    relayCount = [string]$RelayCount
    outputType = 'gpio'
    ledPin     = '2'
  }

  for ($i = 1; $i -le $RelayCount; $i++) {
    $form["relay${i}_pin"] = '255'
  }

  return $form
}

function Test-BoardCrud {
  param([string]$Target, [string]$BaseUrl)

  $suite = 'boards'
  $before = Invoke-ApiWithRetry -Method 'GET' -Url ($BaseUrl + '/api/boards') -TimeoutSec $TimeoutSec
  $activeBefore = if ($before.json) { Normalize-BoardFilename -Filename ([string]$before.json.activeBoardFile) } else { '' }
  $relayCount = if ($before.json) { Get-ActiveRelayCount -BoardsJson $before.json } else { 8 }

  $shortId = Get-TargetShortId -Target $Target
  $stamp = Get-Date -Format 'HHmmss'
  $title = "hb-$shortId-$stamp"
  $form = New-BoardSaveForm -Title $title -RelayCount $relayCount

  $create = Invoke-ApiWithRetry -Method 'POST' -Url ($BaseUrl + '/api/boards') -Form $form -TimeoutSec $TimeoutSec
  $createOk = ($create.status -eq 200 -and $create.json -and $create.json.ok)
  Add-Result -Target $Target -Suite $suite -Test 'create' -Endpoint '/api/boards (POST save)' -Passed $createOk -Details ("status=" + $create.status + ", body=" + $create.body)

  if (-not $createOk) { return }

  $created = Normalize-BoardFilename -Filename ([string]$create.json.filename)
  if (-not $created) { return }

  $listAfterCreate = Invoke-ApiWithRetry -Method 'GET' -Url ($BaseUrl + '/api/boards') -TimeoutSec $TimeoutSec
  $listContainsCreated = $false
  if ($listAfterCreate.json -and $listAfterCreate.json.boards) {
    foreach ($b in $listAfterCreate.json.boards) {
      $f = Normalize-BoardFilename -Filename ([string]$b.filename)
      if ($f -eq $created) {
        $listContainsCreated = $true
        break
      }
    }
  }

  Add-Result -Target $Target -Suite $suite -Test 'list contains created' -Endpoint '/api/boards' -Passed $listContainsCreated -Details ("status=" + $listAfterCreate.status + ", requested=" + $created)
  if (-not $listContainsCreated) { return }

  $renameTitle = "$title-r"
  $rename = Invoke-ApiWithRetry -Method 'POST' -Url ($BaseUrl + '/api/boards') -Form @{ action = 'rename'; filename = $created; title = $renameTitle } -TimeoutSec $TimeoutSec
  $renameOk = ($rename.status -eq 200 -and $rename.json -and $rename.json.ok)
  Add-Result -Target $Target -Suite $suite -Test 'rename' -Endpoint '/api/boards (POST rename)' -Passed $renameOk -Details ("status=" + $rename.status + ", body=" + $rename.body)

  $renamed = if ($renameOk) { Normalize-BoardFilename -Filename ([string]$rename.json.filename) } else { $created }

  $set = Invoke-ApiWithRetry -Method 'POST' -Url ($BaseUrl + '/api/boards') -Form @{ action = 'setactive'; filename = $renamed } -TimeoutSec $TimeoutSec
  $setOk = ($set.status -eq 200 -and $set.json -and $set.json.ok)
  Add-Result -Target $Target -Suite $suite -Test 'setactive' -Endpoint '/api/boards (POST setactive)' -Passed $setOk -Details ("status=" + $set.status + ", body=" + $set.body)

  if ($activeBefore) {
    $restore = Invoke-ApiWithRetry -Method 'POST' -Url ($BaseUrl + '/api/boards') -Form @{ action = 'setactive'; filename = $activeBefore } -TimeoutSec $TimeoutSec
    $restoreOk = ($restore.status -eq 200 -and $restore.json -and $restore.json.ok)
    Add-Result -Target $Target -Suite $suite -Test 'restore active' -Endpoint '/api/boards (POST setactive)' -Passed $restoreOk -Details ("status=" + $restore.status + ", body=" + $restore.body)
  }

  $delete = Invoke-ApiWithRetry -Method 'POST' -Url ($BaseUrl + '/api/boards') -Form @{ action = 'delete'; filename = $renamed } -TimeoutSec $TimeoutSec
  $deleteOk = ($delete.status -eq 200 -and $delete.json -and $delete.json.ok)
  Add-Result -Target $Target -Suite $suite -Test 'delete' -Endpoint '/api/boards (POST delete)' -Passed $deleteOk -Details ("status=" + $delete.status + ", body=" + $delete.body)
}

function Test-WebSocketHome {
  param([string]$Target, [string]$BaseUrl)

  $suite = 'websocket'

  try {
    $uri = [Uri]$BaseUrl
    $wsUri = 'ws://' + $uri.Host + '/ws'

    $ws = [System.Net.WebSockets.ClientWebSocket]::new()
    $cts = [System.Threading.CancellationTokenSource]::new(8000)
    $null = $ws.ConnectAsync([Uri]$wsUri, $cts.Token).GetAwaiter().GetResult()

    $payload = [System.Text.Encoding]::UTF8.GetBytes('home')
    $segment = [System.ArraySegment[byte]]::new($payload)
    $null = $ws.SendAsync($segment, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, $cts.Token).GetAwaiter().GetResult()

    $buffer = New-Object byte[] 4096
    $recvSeg = [System.ArraySegment[byte]]::new($buffer)
    $result = $ws.ReceiveAsync($recvSeg, $cts.Token).GetAwaiter().GetResult()
    $msg = [System.Text.Encoding]::UTF8.GetString($buffer, 0, $result.Count)

    $ok = $msg.StartsWith('{')
    $details = if ($ok) { 'json state received' } else { $msg }
    Add-Result -Target $Target -Suite $suite -Test 'connect+home' -Endpoint '/ws' -Passed $ok -Details $details

    $ws.Dispose()
  } catch {
    Add-Result -Target $Target -Suite $suite -Test 'connect+home' -Endpoint '/ws' -Passed $false -Details $_.Exception.Message
  }
}

foreach ($targetRaw in $Targets) {
  $target = $targetRaw.Trim()
  if (-not $target) { continue }

  $base = Normalize-BaseUrl -Target $target

  Test-BasicEndpoints -Target $target -BaseUrl $base
  Test-TemplateCrud -Target $target -BaseUrl $base

  if ($Mode -eq 'Full') {
    Test-BoardCrud -Target $target -BaseUrl $base
    Test-WebSocketHome -Target $target -BaseUrl $base
  }
}

$passedCount = ($results | Where-Object { $_.passed }).Count
$failedCount = ($results | Where-Object { -not $_.passed }).Count
$finishedAt = Get-Date

$effectiveCatalog = $endpointCatalog | Where-Object {
  $_.mode -eq 'Smoke' -or ($Mode -eq 'Full' -and $_.mode -eq 'Full')
}

$coverage = @()
foreach ($ep in ($effectiveCatalog | Sort-Object endpoint -Unique)) {
  $testsForEndpoint = @($results | Where-Object { $_.endpoint -eq $ep.endpoint })
  $coverage += [pscustomobject]@{
    endpoint = $ep.endpoint
    mode     = $ep.mode
    executed = ($testsForEndpoint.Count -gt 0)
    passed   = ($testsForEndpoint.Count -gt 0 -and (@($testsForEndpoint | Where-Object { $_.passed }).Count -eq $testsForEndpoint.Count))
    runCount = $testsForEndpoint.Count
  }
}

$report = [pscustomobject]@{
  startedAt   = $startedAt
  finishedAt  = $finishedAt
  durationSec = [math]::Round((New-TimeSpan -Start $startedAt -End $finishedAt).TotalSeconds, 2)
  mode        = $Mode
  targets     = $Targets
  summary     = [pscustomobject]@{
    passed = $passedCount
    failed = $failedCount
    total  = $results.Count
  }
  coverage    = $coverage
  tests       = $results
}

$reportJson = $report | ConvertTo-Json -Depth 10
$reportDir = Split-Path -Path $ReportPath -Parent
if ($reportDir -and -not (Test-Path -Path $reportDir)) {
  New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
}
Set-Content -Path $ReportPath -Value $reportJson -Encoding UTF8

Write-Host "Functional harness complete. Passed=$passedCount Failed=$failedCount Total=$($results.Count)"
Write-Host "Report: $ReportPath"

if ($failedCount -gt 0) {
  exit 1
}

exit 0
