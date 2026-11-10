# Packages one language of the mod as an overwrite mod for a test setup: the board's texts and the
# MCM translation files only, to install over the (English) mod that deployProject keeps in MO2.
#   SKSE\Plugins\skyrimnet_MiniGame\strings.json
#   Interface\Translations\skyrimnet_MiniGame_<every game language>.txt
# into dist\skyrimnet_MiniGame_<lang>_overwrite.zip. Players use the FOMOD (build_fomod.ps1) instead.
# usage: tools\package_language.ps1 ja
param([Parameter(Mandatory = $true)][string]$Language)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$name = 'skyrimnet_MiniGame'
$src = "$root\translations\$Language"
if (-not (Test-Path "$src\strings.json")) { throw "no translation at $src" }
& node "$root\tools\check_translation.js" $root $src
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$tsv = Get-ChildItem $src -Filter 'mcm_*.tsv' | Select-Object -First 1 -ExpandProperty FullName
$mod = "$root\dist\overwrite_$Language"
if (Test-Path $mod) { Remove-Item -Recurse -Force $mod }
New-Item -ItemType Directory "$mod\SKSE\Plugins\$name" | Out-Null
Copy-Item "$src\strings.json" "$mod\SKSE\Plugins\$name\strings.json"
foreach ($g in 'CHINESE', 'CZECH', 'ENGLISH', 'FRENCH', 'GERMAN', 'ITALIAN', 'JAPANESE', 'POLISH', 'RUSSIAN', 'SPANISH') {
    & node "$root\tools\make_translation.js" $tsv "$mod\Interface\Translations\${name}_$g.txt" | Out-Null
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = "$root\dist\${name}_${Language}_overwrite.zip"
if (Test-Path $zip) { Remove-Item $zip }
$archive = [System.IO.Compression.ZipFile]::Open($zip, 'Create')
try {
    Get-ChildItem $mod -Recurse -File | ForEach-Object {
        $entry = $_.FullName.Substring($mod.Length + 1).Replace('\', '/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, $entry, 'Optimal') | Out-Null
    }
} finally { $archive.Dispose() }
"packaged $zip"
