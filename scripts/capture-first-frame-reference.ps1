[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Rom,
    [string]$Mesen = "",
    [string]$Output = "analysis\differential\first-frame-reference.json"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$romPath = (Resolve-Path -LiteralPath $Rom).Path
if ([IO.Path]::GetExtension($romPath) -notin @(".sfc", ".smc")) {
    throw "ROM must have an .sfc or .smc extension"
}
if (-not $Mesen) { $Mesen = Join-Path $repoRoot ".tools\mesen-ce\Mesen.exe" }
$mesenPath = (Resolve-Path -LiteralPath $Mesen).Path
$luaPath = Join-Path $repoRoot "tools\mesen\first_frame_oracle.lua"
$privateRoot = Join-Path $repoRoot ".private\frame-oracle"
[IO.Directory]::CreateDirectory($privateRoot) | Out-Null
$rawLog = Join-Path $privateRoot "first-frame-reference.log"
$mesenHome = Join-Path $repoRoot ".private\mesen-home"
$python = Join-Path $repoRoot ".venv\Scripts\python.exe"
if (-not (Test-Path -LiteralPath $python)) {
    $python = (Get-Command python -ErrorAction Stop).Source
}

& $python (Join-Path $repoRoot "tools\trace\kss_trace\run_mesen.py") `
    --mesen $mesenPath --lua $luaPath --rom $romPath --raw-log $rawLog `
    --home $mesenHome --timeout 60
if ($LASTEXITCODE -ne 0) { throw "MesenCE first-frame capture failed with exit code $LASTEXITCODE" }
if (-not (Select-String -LiteralPath $rawLog -Pattern "KSS_FRAME_END_V1" -Quiet)) {
    throw "First-frame capture ended without a completion marker"
}

$outputPath = Join-Path $repoRoot $Output
& $python -m differential.kss_diff.frame_oracle $rawLog $outputPath
if ($LASTEXITCODE -ne 0) { throw "First-frame sanitizer failed with exit code $LASTEXITCODE" }
Write-Output $rawLog
Write-Output $outputPath
