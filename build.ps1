# Builds skyrimnet_MiniGame.dll and copies the DLL, the view and the English texts (strings.json
# and the MCM translation file) into the skyrim-modding-tools-mcp project skyrimnet_MiniGame
# (its assets/), which deployProject turns into the MO2 mod.
# The Japanese translation is a separate mod: C:\Modding\skyrimnet_MiniGame_JP\deploy.ps1.
$project = 'C:\Modding\skyrim-modding-tools-mcp\projects\skyrimnet_MiniGame\assets'
Push-Location $PSScriptRoot
try {
    & 'C:\Program Files\xmake\xmake.exe' build -y
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Copy-Item build\windows\x64\release\skyrimnet_MiniGame.dll "$project\SKSE\Plugins\" -Force
    Copy-Item view\* "$project\PrismaUI\views\skyrimnet_MiniGame\" -Force
    node tools\export_strings.js "$project\SKSE\Plugins\skyrimnet_MiniGame\strings.json"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    node tools\make_translation.js translations\mcm_en.tsv "$project\Interface\Translations\skyrimnet_MiniGame_ENGLISH.txt"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    # The game reads the file of its own language only; without one the MCM shows raw $keys.
    # Every other language gets the English texts (the Japanese mod replaces its own).
    foreach ($language in 'CHINESE', 'CZECH', 'FRENCH', 'GERMAN', 'ITALIAN', 'JAPANESE', 'POLISH', 'RUSSIAN', 'SPANISH') {
        Copy-Item "$project\Interface\Translations\skyrimnet_MiniGame_ENGLISH.txt" "$project\Interface\Translations\skyrimnet_MiniGame_$language.txt" -Force
    }
} finally {
    Pop-Location
}
