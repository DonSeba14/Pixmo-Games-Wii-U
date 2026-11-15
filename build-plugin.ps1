# Compila el plugin de Aroma "Pixmo Games: inicio" (plugin/) con las imágenes de
# wiiu-env (WUPS + librpxloader). Resultado: plugin/pixmo_inicio.wps
#   ./build-plugin.ps1
# En la SD va en SD:/wiiu/environments/aroma/plugins/pixmo_inicio.wps
$ErrorActionPreference = 'Stop'
$root = Join-Path $PSScriptRoot 'plugin'
$tag = 'pixmogames-plugin-builder'

& docker build --quiet -t $tag $root | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'docker build failed' }
& docker run --rm --network none --mount "type=bind,source=$root,target=/project" --workdir /project $tag make -j4
if ($LASTEXITCODE -ne 0) { throw 'plugin build failed' }

$wps = Join-Path $root 'pixmo_inicio.wps'
if (!(Test-Path -LiteralPath $wps)) { throw 'pixmo_inicio.wps was not produced' }
Get-Item -LiteralPath $wps | Select-Object Name, Length, LastWriteTime
