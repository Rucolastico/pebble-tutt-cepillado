#!/bin/bash
# Capturas de la tienda para todas las plataformas, con el ciclo congelado a
# media pantalla (modo CAPTURAS de main.c). Quedan en tienda/capturas/.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."
C=src/c/main.c
cp "$C" "$C.orig"
trap 'mv "$C.orig" "$C"; pebble clean >/dev/null; pebble build >/dev/null' EXIT
sed -i '1i #define CAPTURAS' "$C"
pebble clean >/dev/null   # waf no siempre nota el cambio de una línea
rm -rf screenshots
pebble screenshot --all-platforms --no-open
mkdir -p tienda/capturas
for f in screenshots/*.png; do mv "$f" "tienda/capturas/$(basename "$f" | cut -d_ -f1).png"; done
rmdir screenshots
