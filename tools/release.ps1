# Arma la versión para publicar Pixmo Games:
#   release/PixmoGames-v<versión>.zip         lo que el usuario copia a la SD
#   release/pixmo-games/                      código fuente limpio (listo para un repositorio)
#   release/PixmoGames-v<versión>-source.zip  el mismo código fuente en zip
# Antes: ./build-wiiu.ps1 y ./build-plugin.ps1. Uso: ./tools/release.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$ver = ([regex]'APP_VERSION "([^"]+)"').Match((Get-Content -Raw (Join-Path $root 'src/common.h'))).Groups[1].Value
if (!$ver) { throw 'APP_VERSION not found in src/common.h' }
$name = "PixmoGames-v$ver"

# every interface text must have its English version
& py -3 -I (Join-Path $root 'tools/check_i18n.py')
if ($LASTEXITCODE -ne 0) { throw 'missing translations (tools/check_i18n.py)' }

$wuhb = Join-Path $root 'build-wiiu/pixmogames.wuhb'
$wps = Join-Path $root 'plugin/pixmo_inicio.wps'
foreach ($f in $wuhb, $wps) { if (!(Test-Path -LiteralPath $f)) { throw "$f not found: build it first" } }
$newest = Get-ChildItem (Join-Path $root 'src') -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ((Get-Item $wuhb).LastWriteTime -lt $newest.LastWriteTime) { throw "pixmogames.wuhb is older than src/$($newest.Name): run ./build-wiiu.ps1" }
$pnewest = Get-ChildItem (Join-Path $root 'plugin/src') -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ((Get-Item $wps).LastWriteTime -lt $pnewest.LastWriteTime) { throw 'pixmo_inicio.wps is older than its source: run ./build-plugin.ps1' }

$out = Join-Path $root 'release'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$pkg = Join-Path $out $name
$src = Join-Path $out 'pixmo-games'
foreach ($d in $pkg, $src) { if (Test-Path $d) { Remove-Item -Recurse -Force $d } }

# --- package for the SD card
$app = Join-Path $pkg 'wiiu/apps/pixmogames'
$plug = Join-Path $pkg 'autostart_plugin_optional/wiiu/environments/aroma/plugins'
New-Item -ItemType Directory -Force -Path $app, $plug, (Join-Path $pkg 'licenses') | Out-Null
Copy-Item $wuhb $app
Copy-Item $wps $plug
Copy-Item (Join-Path $root 'README.txt'), (Join-Path $root 'LEEME.txt') $pkg
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $pkg 'LICENSE.txt')
Copy-Item (Join-Path $root 'THIRD_PARTY_NOTICES.md') (Join-Path $pkg 'THIRD_PARTY_NOTICES.txt')
Copy-Item (Join-Path $root 'licenses/*') (Join-Path $pkg 'licenses')
$zip = Join-Path $out "$name.zip"
Compress-Archive -Path (Join-Path $pkg '*') -DestinationPath $zip -Force

# --- clean source code (an allowlist: build output, personal media and test data stay out)
$keep = @(
    'src', 'third_party', 'assets', 'docs', 'licenses',
    'plugin/src', 'plugin/Makefile', 'plugin/Dockerfile',
    'tools/make_assets.py', 'tools/check_i18n.py', 'tools/musica_consola.py', 'tools/descargar_caratulas.py',
    'tools/fake_titles.py', 'tools/pc_run.sh', 'tools/test_adpcm.c', 'tools/release.ps1',
    'CMakeLists.txt', 'build-wiiu.ps1', 'build-plugin.ps1', 'build-pc.sh',
    'README.md', 'README.txt', 'LEEME.txt', 'LICENSE', 'THIRD_PARTY_NOTICES.md', '.gitignore'
)
foreach ($k in $keep) {
    $from = Join-Path $root $k
    if (!(Test-Path -LiteralPath $from)) { throw "missing $k" }
    $to = Join-Path $src $k
    New-Item -ItemType Directory -Force -Path (Split-Path $to -Parent) | Out-Null
    Copy-Item -LiteralPath $from -Destination $to -Recurse -Force
}
# nothing personal may leave with the source: local paths, and the words listed (one
# regex per line) in tools/release_check.local.txt, a private file that is never published
$patterns = @('/mnt/[a-z]/', '[A-Z]:\\+Users\\')
$local = Join-Path $PSScriptRoot 'release_check.local.txt'
if (Test-Path -LiteralPath $local) {
    $patterns += Get-Content -LiteralPath $local | Where-Object { $_.Trim() -and -not $_.StartsWith('#') }
}
$bad = Get-ChildItem $src -Recurse -File | Where-Object { $_.Extension -in '.c', '.h', '.cpp', '.py', '.ps1', '.sh', '.md', '.txt', '.cmake' -or $_.Name -in 'Makefile', 'Dockerfile', 'CMakeLists.txt', '.gitignore' } |
    Select-String -Pattern $patterns -List
if ($bad) { $bad | ForEach-Object { Write-Host "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }; throw 'personal text in the source' }
$szip = Join-Path $out "$name-source.zip"
Compress-Archive -Path $src -DestinationPath $szip -Force

Get-Item $zip, $szip | Select-Object Name, Length
Get-FileHash -Algorithm SHA256 $zip, $szip | Select-Object @{ n = 'File'; e = { Split-Path $_.Path -Leaf } }, Hash
