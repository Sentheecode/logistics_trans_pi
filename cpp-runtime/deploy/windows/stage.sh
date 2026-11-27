#!/usr/bin/env bash
set -euo pipefail

if [[ ${MSYSTEM:-} != MINGW64 || $# != 2 ]]; then
    echo 'Usage in MSYS2 MINGW64: stage.sh BUILD_DIR NEW_STAGE_DIR' >&2
    exit 2
fi

build=$1
stage=$2
source_dir=$(cd -- "$(dirname -- "$0")" && pwd)
source_sha=$(git rev-parse HEAD | tr -d '\r')
if [[ ! $source_sha =~ ^[0-9a-f]{40}$ || ${GITHUB_SHA:-$source_sha} != "$source_sha" ]]; then
    echo 'Could not establish the checked-out source SHA' >&2
    exit 1
fi

mkdir -- "$stage"
mkdir -p -- "$stage/plugins/platforms" "$stage/licenses"

for name in Logistics collector hmi diagnose simulation-archive logistics-mcp; do
    cp -- "$build/$name.exe" "$stage/"
done
if [[ ! -f $build/point-table.sqlite ]]; then
    echo 'Missing point-table.sqlite next to the preview executables' >&2
    exit 1
fi
cp -- "$build/point-table.sqlite" "$stage/point-table.sqlite"
plugins=$(cygpath -u "$(qmake6 -query QT_INSTALL_PLUGINS | tr -d '\r')")
platform_plugin="$plugins/platforms/qwindows.dll"
cp -- "$platform_plugin" "$stage/plugins/platforms/"
printf '[Paths]\nPrefix=.\nPlugins=plugins\n' > "$stage/qt.conf"
cp -- "$source_dir/START.cmd" "$stage/"
cp -- "$source_dir/README.txt" "$stage/"

# Scan PE imports, including the platform plugin. Copy only dependencies of the
# selected executables from their own MINGW64 distribution.
system32=$(cygpath -u "$SYSTEMROOT")/System32
declare -A available staged owners
for dll in /mingw64/bin/*.dll; do
    name=${dll##*/}
    available[${name,,}]=$dll
done
owners[mingw-w64-x86_64-nlohmann-json]=1
owner=$(pacman -Qoq "$platform_plugin")
owners[$owner]=1
pending=("$stage/"*.exe "$stage/plugins/platforms/qwindows.dll")
for ((i=0; i<${#pending[@]}; i++)); do
    imports=$(objdump -p "${pending[i]}")
    while read -r name; do
        key=${name,,}
        [[ -n $key ]] || continue
        if [[ -n ${staged[$key]:-} || $key == api-ms-win-* || $key == ext-ms-win-* ]]; then
            continue
        fi
        if [[ -f "$system32/$name" ]]; then
            continue
        fi
        origin=${available[$key]:-}
        if [[ -z $origin ]]; then
            echo "Unresolved DLL $name from ${pending[i]}" >&2
            exit 1
        fi
        dest="$stage/${origin##*/}"
        cp -- "$origin" "$dest"
        pending+=("$dest")
        staged[$key]=1
        owner=$(pacman -Qoq "$origin")
        owners[$owner]=1
    done < <(awk '/DLL Name:/ { print $NF }' <<< "$imports" | tr -d '\r')
done

{
    printf 'source %s\n' "$source_sha"
    printf 'environment %s / %s\n' "$MSYSTEM" "$(gcc -dumpmachine)"
    gcc -dumpfullversion
    cmake --version
    printf 'Qt %s\n\n' "$(qmake6 -query QT_VERSION)"
    printf 'MSYS2 binary package versions (corresponding source packages at https://packages.msys2.org/)\n'
    for owner in "${!owners[@]}"; do
        pacman -Q "$owner"
        printf 'https://packages.msys2.org/package/%s\n' "$owner"
        while IFS= read -r file; do
            [[ $file == /mingw64/share/licenses/* && -f $file ]] || continue
            relative=${file#/mingw64/share/licenses/}
            mkdir -p -- "$stage/licenses/${relative%/*}"
            cp -L -- "$file" "$stage/licenses/$relative"
        done < <(pacman -Qlq "$owner")
    done
} > "$stage/BUILD-INFO.txt"

(cd -- "$stage" && find . -type f ! -name FILES.txt -print | LC_ALL=C sort) > "$stage/FILES.txt"
