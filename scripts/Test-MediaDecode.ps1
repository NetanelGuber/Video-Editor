[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]]$Inventory,
    [Parameter(Mandatory)][string]$FFmpeg,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../evidence/session-0/decode')
)
$ErrorActionPreference = 'Stop'
$decoder = (Get-Command $FFmpeg -ErrorAction Stop).Source
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$results = @()
foreach ($inventoryPath in $Inventory) {
    $records = (Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json).files
    foreach ($record in $records) {
        $logName = '{0}-{1}.txt' -f (Split-Path (Split-Path $inventoryPath -Parent) -Leaf), $record.id
        $output = & $decoder -hide_banner -v error -xerror -nostdin -hwaccel none -i $record.path -map '0:v:0' -map '0:a?' -t 5 -f null - 2>&1
        $exitCode = $LASTEXITCODE
        @("Input: $($record.path)", 'Scope: first 5 seconds (whole file for shorter fixtures); CPU video and all audio streams', "Exit code: $exitCode") + @($output) |
            Set-Content -LiteralPath (Join-Path $OutputDirectory $logName) -Encoding utf8
        $results += [ordered]@{path=$record.path; scope='first 5 seconds or EOF; CPU video and all audio streams'; exitCode=$exitCode; log=$logName}
        if ($exitCode -ne 0) { throw "CPU decode failed for $($record.path); see $logName" }
        Write-Host "CPU decode passed: $(Split-Path $record.path -Leaf)"
    }
}
[ordered]@{schemaVersion=1; checkedAtUtc=[DateTime]::UtcNow.ToString('o'); ffmpegPath=$decoder; ffmpegVersion=((& $decoder -version | Select-Object -First 1) -join ''); results=$results} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'results.json') -Encoding utf8
