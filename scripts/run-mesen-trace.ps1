[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Rom,
    [string]$Mesen = "",
    [string]$OutputName = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$romPath = (Resolve-Path -LiteralPath $Rom).Path
if ([IO.Path]::GetExtension($romPath) -notin @(".sfc", ".smc")) {
    throw "ROM must have an .sfc or .smc extension"
}

if (-not $Mesen) {
    $Mesen = Join-Path $repoRoot ".tools\mesen-ce\Mesen.exe"
}
$mesenPath = (Resolve-Path -LiteralPath $Mesen).Path
$luaPath = (Resolve-Path (Join-Path $repoRoot "tools\mesen\dual_cpu_trace.lua")).Path
$privateTraceRoot = Join-Path $repoRoot ".private\traces"
[IO.Directory]::CreateDirectory($privateTraceRoot) | Out-Null

if (-not $OutputName) {
    $OutputName = "mesen-dual-{0}.log" -f (Get-Date -Format "yyyyMMdd-HHmmss")
}
if ([IO.Path]::GetFileName($OutputName) -ne $OutputName -or [IO.Path]::GetExtension($OutputName) -ne ".log") {
    throw "OutputName must be a plain .log filename without directory components"
}
$rawLog = Join-Path $privateTraceRoot $OutputName
$mesenHome = Join-Path $repoRoot ".private\mesen-home"
$python = Join-Path $repoRoot ".venv\Scripts\python.exe"
if (-not (Test-Path -LiteralPath $python)) {
    $python = (Get-Command python -ErrorAction Stop).Source
}

& $python (Join-Path $repoRoot "tools\trace\kss_trace\run_mesen.py") `
    --mesen $mesenPath --lua $luaPath --rom $romPath --raw-log $rawLog `
    --home $mesenHome --timeout 30
if ($LASTEXITCODE -ne 0) {
    throw "MesenCE trace runner failed with exit code $LASTEXITCODE; private log: $rawLog"
}
if (-not (Select-String -LiteralPath $rawLog -Pattern "KSS_TRACE_END_V1" -Quiet)) {
    throw "MesenCE exited without a trace end marker; private log: $rawLog"
}

Write-Output $rawLog
