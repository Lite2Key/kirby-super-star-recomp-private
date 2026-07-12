param(
    [Parameter(Mandatory = $true)]
    [string]$RomPath
)

$ErrorActionPreference = 'Stop'
$expectedSize = 4194304
$expectedSha256 = '4E095FBBDEC4A16B075D7140385FF68B259870CA9E3357F076DFFF7F3D1C4A62'

$item = Get-Item -LiteralPath $RomPath
if ($item.Length -ne $expectedSize) {
    throw "Unsupported ROM size $($item.Length); expected $expectedSize bytes."
}

$actualSha256 = (Get-FileHash -LiteralPath $RomPath -Algorithm SHA256).Hash.ToUpperInvariant()
if ($actualSha256 -ne $expectedSha256) {
    throw "Unsupported ROM SHA-256 $actualSha256; expected $expectedSha256."
}

[pscustomobject]@{
    Path = $item.FullName
    Size = $item.Length
    Sha256 = $actualSha256
    Revision = 'USA rev 0'
    Accepted = $true
} | Format-List
