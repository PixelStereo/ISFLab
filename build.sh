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
        Darwin)
            # The bundle carries the version and commit in its name: open the one this build produced,
            # remove bundles left by earlier commits, and quit a previous build still running
            # (macOS would otherwise bring that instance forward instead of launching the new one).
            app_name="$(sed -n 's/.*"name":"\([^"]*\)".*/\1/p' build/application.json)"
            app="$PWD/build/$app_name.app"
            [[ -n "$app_name" && -d "$app" ]] || { echo "Built application not found: $app" >&2; exit 1; }
            for stale in "$PWD"/build/ISFLab\ v*.app; do
                [[ -d "$stale" && "$stale" != "$app" ]] && rm -rf "$stale"
            done
            pkill -f "$PWD/build/ISFLab v.*\.app/Contents/MacOS/" 2>/dev/null && sleep 1
            open -n "$app" ;;
        MINGW*|MSYS*|CYGWIN*)
            executable="$(ls "$PWD/build/$config"/ISFLab*.exe | head -1)"
            [[ -f "$executable" ]] || executable="$(ls "$PWD"/build/ISFLab*.exe | head -1)"
            "$executable" & ;;
        *) "$PWD/build/ISFLab" & ;;
    esac
fi
