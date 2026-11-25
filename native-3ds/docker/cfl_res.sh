# Sourced by emu-web.sh and emu-test.sh. MELEE_CFL_RES=/path/to/CFL_Res.dat (your console's Mii resource, dumped for
# example with GodMode9) is packed as its system data title into build/cfl-res/cfl-res.cia (git
# ignores build/), and each Azahar profile the launcher sets up installs it. Without it Azahar uses its open-source
# stand-in, whose Mii heads have no real face parts.
#   cfl_res_cia VOLUME NATIVE_DIR   -> prints the CIA's path on the host, or nothing when MELEE_CFL_RES is unset
cfl_res_cia() {
    [ -n "${MELEE_CFL_RES:-}" ] || return 0
    [ -f "$MELEE_CFL_RES" ] || { echo "MELEE_CFL_RES: no file at $MELEE_CFL_RES" >&2; exit 2; }
    mkdir -p "$2/build/cfl-res"
    docker run --rm --entrypoint python3 -v "$(cd "$(dirname "$MELEE_CFL_RES")" && pwd)/$(basename "$MELEE_CFL_RES")":/in/CFL_Res.dat:ro \
        -v "$2/tools":/tools:ro -v "$2/build/cfl-res":/out -v "$1":/work:ro melee3ds-builder \
        /tools/cfl_res_cia.py /in/CFL_Res.dat /out/cfl-res.cia >&2
    echo "$2/build/cfl-res/cfl-res.cia"
}
