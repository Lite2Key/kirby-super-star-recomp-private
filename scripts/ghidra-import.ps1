param(
    [Parameter(Mandatory = $true)]
    [string]$RomPath,
    [switch]$NoAnalysis,
    [string]$ProjectName = 'KSS'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$headless = Join-Path $root '.tools\ghidra\ghidra_12.1.2_PUBLIC\support\analyzeHeadless.bat'
$jdk = (Get-ChildItem (Join-Path $root '.tools\jdk21') -Directory | Select-Object -First 1).FullName
$projectRoot = Join-Path $root 'ghidra-projects'
$stagedRom = Join-Path $projectRoot 'KSS.sfc'
$settings = Join-Path $root '.private\ghidra-home'

if (-not (Test-Path -LiteralPath $headless)) { throw 'Pinned Ghidra is not installed under .tools.' }
if (-not $jdk -or -not (Test-Path -LiteralPath (Join-Path $jdk 'bin\java.exe'))) { throw 'Pinned JDK 21 is not installed under .tools.' }

& (Join-Path $PSScriptRoot 'validate-rom.ps1') -RomPath $RomPath | Out-Null
New-Item -ItemType Directory -Force -Path $projectRoot, $settings | Out-Null
Copy-Item -LiteralPath $RomPath -Destination $stagedRom -Force

$drive = @('R:', 'S:', 'T:', 'U:') | Where-Object { -not (Test-Path "$_\") } | Select-Object -First 1
if (-not $drive) { throw 'No temporary drive letter is available for Ghidra path normalization.' }

subst.exe $drive $root
try {
    $env:JAVA_HOME = "$drive\.tools\jdk21\$(Split-Path $jdk -Leaf)"
    $env:Path = "$env:JAVA_HOME\bin;C:\Windows\System32;C:\Windows"
    $env:USERPROFILE = "$drive\.private\ghidra-home"
    $env:APPDATA = $env:USERPROFILE
    $env:LOCALAPPDATA = $env:USERPROFILE

    $arguments = @("$drive\ghidra-projects", $ProjectName, '-import', "$drive\ghidra-projects\KSS.sfc", '-overwrite')
    if ($NoAnalysis) { $arguments += '-noanalysis' }
    & "$drive\.tools\ghidra\ghidra_12.1.2_PUBLIC\support\analyzeHeadless.bat" @arguments
    if ($LASTEXITCODE -ne 0) { throw "Ghidra import failed with exit code $LASTEXITCODE." }
}
finally {
    subst.exe $drive /D | Out-Null
    $resolved = [IO.Path]::GetFullPath($stagedRom)
    if (-not $resolved.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to clean a staged path outside the repository.'
    }
    Remove-Item -LiteralPath $resolved -Force -ErrorAction SilentlyContinue
}

Write-Host "Private Ghidra project updated at $projectRoot\$ProjectName.gpr"
