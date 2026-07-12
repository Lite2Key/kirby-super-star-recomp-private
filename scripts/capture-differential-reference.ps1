[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Rom,
    [string]$Mesen = "",
    [string]$OutputName = "reset-reference.log"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$romPath = (Resolve-Path -LiteralPath $Rom).Path
if ([IO.Path]::GetExtension($romPath) -notin @(".sfc", ".smc")) {
    throw "ROM must have an .sfc or .smc extension"
}
if ([IO.Path]::GetFileName($OutputName) -ne $OutputName -or [IO.Path]::GetExtension($OutputName) -ne ".log") {
    throw "OutputName must be a plain .log filename"
}
if (-not $Mesen) {
    $Mesen = Join-Path $repoRoot ".tools\mesen-ce\Mesen.exe"
}
$mesenPath = (Resolve-Path -LiteralPath $Mesen).Path
$luaPath = (Resolve-Path (Join-Path $repoRoot "tools\mesen\differential_trace.lua")).Path
$privateRoot = Join-Path $repoRoot ".private\differential"
[IO.Directory]::CreateDirectory($privateRoot) | Out-Null
$rawLog = Join-Path $privateRoot $OutputName
$mesenHome = Join-Path $repoRoot ".private\mesen-home"
$python = Join-Path $repoRoot ".venv\Scripts\python.exe"
if (-not (Test-Path -LiteralPath $python)) {
    $python = (Get-Command python -ErrorAction Stop).Source
}

& $python (Join-Path $repoRoot "tools\trace\kss_trace\run_mesen.py") `
    --mesen $mesenPath --lua $luaPath --rom $romPath --raw-log $rawLog `
    --home $mesenHome --timeout 30
if ($LASTEXITCODE -ne 0) {
    throw "MesenCE differential capture failed with exit code $LASTEXITCODE"
}
if (-not (Select-String -LiteralPath $rawLog -Pattern "KSS_DIFF_END_V1" -Quiet)) {
    throw "Differential capture ended without a completion marker"
}
Write-Output $rawLog
