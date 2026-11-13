#!/bin/sh
# cl_isolate.sh - aisla la libreria estatica del cliente (cl_dll) de la del servidor (hl)
#
# En la Wii el cliente y el servidor del juego son dos librerias estaticas que
# acaban en un solo ELF. Con la prediccion de armas el cliente compila sus
# propias versiones de CBasePlayerWeapon, CBaseEntity, gpGlobals... con el
# mismo nombre que las del servidor pero con otro contenido (#ifdef CLIENT_DLL),
# y el enlazador las mezclaria (vtables, typeinfo, variables globales).
#
# Solucion: renombrar dentro de la libreria del cliente TODO simbolo global
# definido en ella, salvo la tabla de exportaciones que lee el motor. Las
# referencias entre archivos de la misma libreria se renombran igual, asi que
# siguen unidas entre si, y las que apuntan a simbolos de otras librerias
# (motor, pm_shared del servidor, libc) no se tocan.
#
# uso: cl_isolate.sh <nm> <objcopy> <libreria.a> [simbolo_a_conservar ...]
set -e

NM="$1"; OBJCOPY="$2"; LIB="$3"
shift 3

[ -f "$LIB" ] || { echo "cl_isolate: no existe $LIB" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

KEEP="lib_cl_dll_exports $*"

# nombres globales o debiles definidos (tipos T D B R C V W u ... en mayuscula)
"$NM" -g --defined-only "$LIB" 2>/dev/null | awk 'NF==3 && $2 ~ /^[A-Za-z]$/ {print $3}' | sort -u > "$TMP/defs"

for k in $KEEP; do
	grep -vx -- "$k" "$TMP/defs" > "$TMP/defs2" || true
	mv "$TMP/defs2" "$TMP/defs"
done

awk '{print $1" cldll_"$1}' "$TMP/defs" > "$TMP/map"

[ -s "$TMP/map" ] || { echo "cl_isolate: no hay simbolos que renombrar en $LIB" >&2; exit 1; }

"$OBJCOPY" --redefine-syms="$TMP/map" "$LIB" "$TMP/out.a"
cp "$TMP/out.a" "$LIB"

echo "cl_isolate: $(wc -l < "$TMP/map") simbolos renombrados en $LIB"
