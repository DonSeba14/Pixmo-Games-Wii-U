#!/bin/bash
# Ejecuta la versión de PC sin ventana con un guion de entradas y guarda capturas.
#   tools/pc_run.sh "<frame>:<cmd>[=arg] ..."   (ver main.c, script_step)
cd "$(dirname "$0")/.."
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
export LUDO_PCDATA=${LUDO_PCDATA:-pcdata} LUDO_SHOTDIR=${LUDO_SHOTDIR:-build-pc/shots}
export LUDO_SCRIPT="$1"
mkdir -p "$LUDO_SHOTDIR"
timeout ${TIMEOUT:-120} ./build-pc/ludoteca
