#!/usr/bin/env bash
# Compile ISFLab locally in build/ and open it after a successful build.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

config=Debug
tests=OFF
run=1
extra=()
for option in "$@"; do
    case "$option" in
        --debug) config=Debug ;;
        --tests) tests=ON ;;
        --no-run) run=0 ;;
        --no-install) ;; # This script never installs dependencies.
        -D*) extra+=("$option") ;;
        -h|--help)
            echo "Usage: ./build.sh [--debug] [--tests] [--no-run] [-DNAME=VALUE ...]"
            exit 0 ;;
        *) echo "Unknown argument: $option" >&2; exit 2 ;;
    esac
done

qt_hint=()
if [[ "$(uname -s)" == Darwin ]] && command -v brew >/dev/null 2>&1; then
    for formula in qt qtbase; do
        if qt_prefix="$(brew --prefix "$formula" 2>/dev/null)" &&
           [[ -f "$qt_prefix/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
            qt_hint=(-DCMAKE_PREFIX_PATH="$qt_prefix")
            break
        fi
    done
fi

cmake -S . -B build -DCMAKE_BUILD_TYPE="$config" -DBUILD_TESTING="$tests" ${qt_hint[@]+"${qt_hint[@]}"} ${extra[@]+"${extra[@]}"}
cmake --build build --config "$config" --parallel

if [[ "$run" == 1 ]]; then
    case "$(uname -s)" in
        Darwin) open "$(ls -d "$PWD"/build/ISFLab*.app | head -1)" ;;
        MINGW*|MSYS*|CYGWIN*)
            executable="$(ls "$PWD/build/$config"/ISFLab*.exe | head -1)"
            [[ -f "$executable" ]] || executable="$(ls "$PWD"/build/ISFLab*.exe | head -1)"
            "$executable" & ;;
        *) "$PWD/build/ISFLab" & ;;
    esac
fi
