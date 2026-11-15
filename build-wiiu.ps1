# Compila Pixmo Games para Wii U con devkitPro (Docker) y arma el zip para la SD.
#   ./build-wiiu.ps1 [-Jobs 8]
# Resultado: build-wiiu/pixmogames.wuhb y build-wiiu/pixmogames-sd.zip
param([int]$Jobs = 8)
$ErrorActionPreference = 'Stop'
$image = 'devkitpro/devkitppc@sha256:44cb1a920e1ec3ec7c06767493c3b85f8d643d6137cc4661f0201895ac6e4967'
$root = $PSScriptRoot

if (!(Test-Path (Join-Path $root 'assets/icon.png'))) {
    & py -3 (Join-Path $root 'tools/make_assets.py')
    if ($LASTEXITCODE -ne 0) { throw 'make_assets.py failed (needs Python 3 + Pillow)' }
}

# pipefail: a compile error must stop the script (grep at the end of the pipe would hide it)
$cmd = 'set -e -o pipefail; ' +
       '/opt/devkitpro/portlibs/wiiu/bin/powerpc-eabi-cmake -S . -B build-wiiu -DCMAKE_BUILD_TYPE=Release >/dev/null; ' +
       'cmake --build build-wiiu --parallel ' + $Jobs + ' 2>&1 | { grep -v -E "^\[ *[0-9]+%\] Building" || true; }; ' +
       '/opt/devkitpro/devkitPPC/bin/powerpc-eabi-size build-wiiu/pixmogames.elf'
& docker run --rm --network none --mount "type=bind,source=$root,target=/project" --workdir /project $image bash -lc $cmd
if ($LASTEXITCODE -ne 0) { throw 'Wii U build failed' }

$bundle = Join-Path $root 'build-wiiu/pixmogames.wuhb'
if (!(Test-Path -LiteralPath $bundle)) { throw 'pixmogames.wuhb was not produced' }

# SD layout: SD:/wiiu/apps/pixmogames/pixmogames.wuhb (+ covers/ and logos/ folders)
$stage = Join-Path $root 'build-wiiu/sd'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
$dest = Join-Path $stage 'wiiu/apps/pixmogames'
New-Item -ItemType Directory -Force -Path (Join-Path $dest 'covers'), (Join-Path $dest 'logos') | Out-Null
Copy-Item -LiteralPath $bundle -Destination $dest -Force
Copy-Item -LiteralPath (Join-Path $root 'LEEME.txt') -Destination $stage -Force
Set-Content -LiteralPath (Join-Path $dest 'covers/LEEME.txt') -Encoding UTF8 -Value @'
Carátulas propias: guarda aquí imágenes JPG o PNG con el ID de GameTDB del juego
(por ejemplo ARPE01.jpg) o con su Title ID de 16 cifras (0005000010101C00.png).
Pixmo Games también descarga aquí las carátulas de GameTDB si la opción está activada.
'@
Set-Content -LiteralPath (Join-Path $dest 'logos/LEEME.txt') -Encoding UTF8 -Value @'
Logos de categorías: copia aquí imágenes PNG o JPG (cuadradas, 128x128 o más)
y elígelas en Ajustes > Gestionar categorías > (categoría) > Logo personalizado.
'@
# Aroma plugin that opens Pixmo Games at power-on / after a game (./build-plugin.ps1)
$plugin = Join-Path $root 'plugin/pixmo_inicio.wps'
if (Test-Path -LiteralPath $plugin) {
    $pdir = Join-Path $stage 'wiiu/environments/aroma/plugins'
    New-Item -ItemType Directory -Force -Path $pdir | Out-Null
    Copy-Item -LiteralPath $plugin -Destination $pdir -Force
}
$zip = Join-Path $root 'build-wiiu/pixmogames-sd.zip'
Compress-Archive -Path (Join-Path $stage 'wiiu'), (Join-Path $stage 'LEEME.txt') -DestinationPath $zip -Force
Get-FileHash -Algorithm SHA256 -LiteralPath $bundle, $zip
