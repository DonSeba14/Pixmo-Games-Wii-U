#!/bin/bash
# Compila la versión de PC (solo pruebas) en WSL/Linux con SDL2.
set -e
cd "$(dirname "$0")"
mkdir -p build-pc
gcc -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
    -Isrc -Ithird_party $(sdl2-config --cflags) src/*.c -o build-pc/ludoteca $(sdl2-config --libs) -lm -lpthread
echo "build-pc/ludoteca"
