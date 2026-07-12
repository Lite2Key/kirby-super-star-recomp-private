param(
    [string]$ProjectName = 'KSS',
    [string]$ProgramName = 'KSS.sfc',
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$headless = Join-Path $root '.tools\ghidra\ghidra_12.1.2_PUBLIC\support\analyzeHeadless.bat'
$jdk = Get-ChildItem (Join-Path $root '.tools\jdk21') -Directory | Select-Object -First 1
$projectRoot = Join-Path $root 'ghidra-projects'
$settings = Join-Path $root '.private\ghidra-home'
if (-not $OutputPath) { $OutputPath = Join-Path $root '.private\ghidra-export\sanitized-program.json' }

if (-not (Test-Path -LiteralPath $headless)) { throw 'Pinned Ghidra 12.1.2 is not installed under .tools.' }
if (-not $jdk -or -not (Test-Path -LiteralPath (Join-Path $jdk.FullName 'bin\java.exe'))) {
    throw 'Pinned JDK 21 is not installed under .tools.'
}
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot "$ProjectName.gpr"))) {
    throw "Ghidra project does not exist: $ProjectName"
}

$resolvedOutput = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($resolvedOutput)), $settings | Out-Null
$drive = @('R:', 'S:', 'T:', 'U:') | Where-Object { -not (Test-Path "$_\") } | Select-Object -First 1
if (-not $drive) { throw 'No temporary drive letter is available for Ghidra path normalization.' }

$outputIsInRoot = $resolvedOutput.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
subst.exe $drive $root
try {
    $env:JAVA_HOME = "$drive\.tools\jdk21\$($jdk.Name)"
    $env:Path = "$env:JAVA_HOME\bin;C:\Windows\System32;C:\Windows"
    $env:USERPROFILE = "$drive\.private\ghidra-home"
    $env:APPDATA = $env:USERPROFILE
    $env:LOCALAPPDATA = $env:USERPROFILE
    $outputForGhidra = if ($outputIsInRoot) {
        $relativeOutput = $resolvedOutput.Substring($root.Length).TrimStart('\', '/')
        "$drive\$relativeOutput"
    } else { $resolvedOutput }
    & "$drive\.tools\ghidra\ghidra_12.1.2_PUBLIC\support\analyzeHeadless.bat" `
        "$drive\ghidra-projects" $ProjectName '-process' $ProgramName '-noanalysis' `
        '-scriptPath' "$drive\tools\ghidra" '-postScript' 'ExportSanitizedProgram.java' $outputForGhidra
    if ($LASTEXITCODE -ne 0) { throw "Ghidra export failed with exit code $LASTEXITCODE." }
}
finally { subst.exe $drive /D | Out-Null }

$python = Join-Path $root '.venv\Scripts\python.exe'
if (Test-Path -LiteralPath $python) {
    & $python (Join-Path $root 'tools\ghidra\validate_export.py') $resolvedOutput `
        '--schema' (Join-Path $root 'schemas\ghidra\sanitized-program.schema.json')
    if ($LASTEXITCODE -ne 0) { throw 'The exported JSON did not pass ROM-free schema validation.' }
}
Write-Host "Sanitized Ghidra metadata exported to $resolvedOutput"
