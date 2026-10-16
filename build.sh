#!/bin/sh
# SDL-dlink Switch build: SDL3 static libs -> sdl3.dnro for sdmc:/lib/.
#
# sdl3.dnro carries SDL3 with zero dk*/egl* imports: deko3d is bound at
# runtime from neko3d.dnro (see src/render/deko3d/) and EGL is compiled out.
# Everything else it imports must resolve from the dlink host (verified by
# the ABI check below); the leaf libc symbols no host provides are
# implemented in-tree (src/stdlib/switch/, via SDL_DNRO_BUILD).
#
# Usage: ./build.sh [--smoke]
#   --smoke  also build the smoke test NRO and stage dist/switch/smoke.nro.
# Env: HOST_ELF: optional host ELF to verify sdl3 imports against.
set -eu

usage() {
    echo "usage: $0 [--smoke]" >&2
}

SMOKE=0
for a in "$@"; do
    case "$a" in
        --smoke) SMOKE=1 ;;
        *) usage; exit 1 ;;
    esac
done

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
DIST="$ROOT/dist"
BUILD="$ROOT/build-switch"
DNRO="$ROOT/dnro"
DLINK="$ROOT/extern/libnx-dlink"
NEKO="$ROOT/extern/neko3d"
NEKO_ELF="$NEKO/neko3d-module/neko3d.elf"

if [ -z "${DEVKITPRO:-}" ]; then
    if [ -d "/opt/devkitpro" ]; then
        DEVKITPRO="/opt/devkitpro"
        export DEVKITPRO
    else
        echo "error: DEVKITPRO is not set" >&2
        exit 1
    fi
fi

DKA64="$DEVKITPRO/devkitA64"
CC="$DKA64/bin/aarch64-none-elf-gcc"
NM="$DKA64/bin/aarch64-none-elf-nm"
PY=python3
ARCH="-march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC"

# newer <target> <roots...>: true when target is missing or any file under
# roots is newer. Pure mtime vs real artifacts (no state files), so each
# expensive step below is skipped on no-op builds. One caveat: deleting a
# source leaves no newer file behind, so force with `rm -rf build-switch`
# (SDL) after deletions.
newer() {
    tgt=$1; shift
    [ -e "$tgt" ] || return 0
    roots=""
    for r in "$@"; do
        [ -e "$r" ] && roots="$roots $r"
    done
    [ -z "$roots" ] && return 1
    # unquoted roots: intentional word splitting into find roots
    # shellcheck disable=SC2086
    [ -n "$(find $roots -newer "$tgt" -print -quit 2>/dev/null)" ]
}

NEKOMOD="$NEKO/neko3d-module"

# 1. neko3d module (provides dk* at runtime + the def for the ABI check).
if newer "$NEKOMOD/neko3d.dnro" "$NEKOMOD/Makefile" "$NEKOMOD/stub" \
        "$NEKO/source" "$NEKO/include"; then
    make -C "$NEKOMOD"
fi

# 2. SDL3 Switch static build (EGL/GL off: no provider exists for a dnro).
if newer "$BUILD/CMakeCache.txt" "$ROOT/CMakeLists.txt" "$ROOT/cmake" \
        "$ROOT/src" "$ROOT/include"; then
    cmake -S "$ROOT" -B "$BUILD" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF \
        -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF \
        -DSDL_DNRO_BUILD=ON \
        -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON > /dev/null
fi
if newer "$BUILD/libSDL3.a" "$ROOT/CMakeLists.txt" "$ROOT/cmake" \
        "$ROOT/src" "$ROOT/include" "$NEKO/include" "$DLINK/include"; then
    cmake --build "$BUILD"
fi

# 3+4. Module entry stub (main is required by module.specs, never called)
# and link: whole SDL archive + stub. (libSDL_uclibc.a is a subset already
# inside libSDL3.a; linking both duplicates symbols.)
# -z nopack-relative-relocs: Horizon's ro applies RELA but not RELR, so
# packed relative relocs would leave static data pointers unrelocated
# (same reason the qlaunch menu links with it).
if newer "$DNRO/sdl3.dnro" "$BUILD/libSDL3.a" "$DNRO/stub" \
        "$DLINK/tools/module.specs" "$ROOT/build.sh"; then
    mkdir -p "$DNRO/build/stub"
    $CC -g $ARCH -c "$DNRO/stub/stub.c" -o "$DNRO/build/stub/stub.o"
    $CC -g $ARCH -c "$DNRO/stub/crt0.s" -o "$DNRO/build/stub/crt0.o"
    $CC -specs="$DLINK/tools/module.specs" -g $ARCH -shared -nodefaultlibs \
        -Wl,--export-dynamic -Wl,-z,nopack-relative-relocs -Wl,-Map,"$DNRO/sdl3.map" \
        -o "$DNRO/sdl3.elf" \
        "$DNRO/build/stub/stub.o" "$DNRO/build/stub/crt0.o" \
        -Wl,--whole-archive "$BUILD/libSDL3.a" -Wl,--no-whole-archive
    "$NM" -D --defined-only "$DNRO/sdl3.elf" | awk '{print $NF}' | sort -u > "$DNRO/sdl3.def"
    "$NM" -D -u "$DNRO/sdl3.elf" | awk '{print $NF}' | sort -u > "$DNRO/sdl3.undef"
    "$PY" "$DLINK/tools/elf2dnro.py" "$DNRO/sdl3.elf" "$DNRO/sdl3.dnro"
fi

# 5. ABI check (needs HOST_ELF): every sdl3 import must resolve from the
# host or the neko3d module. A dnro that imports what nobody provides dies
# at dlopen with a bare dlink COULD_NOT_RESOLVE; fail the build instead.
if [ -z "${HOST_ELF:-}" ]; then
    echo "note: HOST_ELF unset, skipping host ABI check"
else
    if [ ! -f "$HOST_ELF" ]; then
        echo "error: host $HOST_ELF not found" >&2
        exit 1
    fi
    # Note: .def files may carry CRLF (Windows checkouts); strip CR so comm
    # compares cleanly.
    { "$NM" -D --defined-only "$HOST_ELF" | awk '{print $NF}'
      cat "$NEKO/neko3d-module/neko3d.def"
    } | tr -d '\r' | sort -u > "$DNRO/build.hosthave"
    MISSING=$(comm -23 "$DNRO/sdl3.undef" "$DNRO/build.hosthave" || true)
    rm -f "$DNRO/build.hosthave"
    if [ -n "$MISSING" ]; then
        echo "error: sdl3.dnro imports the host does not provide:" >&2
        echo "$MISSING" >&2
        exit 1
    fi
    echo "abi check: sdl3 imports all resolve"
fi

# 6. Stage the libraries (conversion lives in the link block).
mkdir -p "$DIST/lib"
cp "$DNRO/sdl3.dnro" "$DIST/lib/sdl3.dnro"
cp "$NEKOMOD/neko3d.dnro" "$DIST/lib/neko3d.dnro"

# 7. Optional smoke test NRO.
if [ "$SMOKE" -ne 0 ]; then
    # libdlink for the smoke host link.
    if newer "$DLINK/lib/libdlink.a" "$DLINK/lib/dlink" "$DLINK/include" "$DLINK/Makefile"; then
        make -C "$DLINK"
    fi
    # Host -u roots: every symbol sdl3.dnro and neko3d.dnro import, so the
    # smoke host links them in and dlink_register_host() can expose them.
    "$NM" -D -u "$NEKO_ELF" | awk '{print $NF}' | sort -u > "$DNRO/build.nekoneed"
    cat "$DNRO/sdl3.undef" "$DNRO/build.nekoneed" | sort -u > "$DNRO/build.hostneed"
    rm -f "$DNRO/build.nekoneed"
    { printf 'HOSTNEEDFLAGS :=';
      awk '{printf " -Wl,-u,%s", $1}' "$DNRO/build.hostneed";
      printf '\n';
    } > "$ROOT/smoke/hostneed.mk.new"
    rm -f "$DNRO/build.hostneed"
    if [ ! -f "$ROOT/smoke/hostneed.mk" ] || \
            ! cmp -s "$ROOT/smoke/hostneed.mk.new" "$ROOT/smoke/hostneed.mk"; then
        mv "$ROOT/smoke/hostneed.mk.new" "$ROOT/smoke/hostneed.mk"
        rm -f "$ROOT/smoke/smoke.elf" # flags aren't make deps: force relink
    else
        rm -f "$ROOT/smoke/hostneed.mk.new"
    fi
    if newer "$ROOT/smoke/smoke.nro" "$ROOT/smoke/main.c" "$ROOT/smoke/Makefile" \
            "$ROOT/smoke/hostneed.mk" "$DLINK/lib/libdlink.a"; then
        make -C "$ROOT/smoke"
    fi
    mkdir -p "$DIST/switch"
    cp "$ROOT/smoke/smoke.nro" "$DIST/switch/smoke.nro"
fi

echo "copy dist/lib/* -> sdmc:/lib/"
if [ "$SMOKE" -ne 0 ]; then
    echo "copy dist/switch/* -> sdmc:/switch/"
fi
echo "built at:"
find "$DIST" -type f | sort
