#!/usr/bin/env bash
set -euo pipefail
# Build the exact user-supplied Rubic0n LuaJIT source under extern/LuaJIT.
# Linux + macOS native builds; Android uses its NDK ExternalProject target.
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
source="$root/extern/LuaJIT"
runner_temp="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
work="$runner_temp/arenamp-rubic0n-build"
prefix="$runner_temp/arenamp-rubic0n"
[[ -f "$source/src/Makefile" && -f "$source/src/msvcbuild.bat" && -f "$source/COPYRIGHT" ]] || {
  echo 'Rubic0n LuaJIT sources are missing from extern/LuaJIT' >&2; exit 1;
}
rm -rf "$work" "$prefix"
mkdir -p "$work" "$prefix"
cp -a "$source/." "$work/"
jobs="${RUBICON_BUILD_JOBS:-2}"
case "$(uname -s)" in
  Linux) make -C "$work" -j"$jobs" ;;
  Darwin) make -C "$work" -j"$jobs" TARGET_DYLIBPATH='@rpath/libluajit-5.1.2.dylib' ;;
  *) echo 'Only Linux and macOS are supported by this build helper' >&2; exit 1 ;;
esac
make -C "$work" PREFIX="$prefix" MULTILIB=lib install
[[ -f "$prefix/include/luajit-2.1/lua.hpp" ]] || { echo 'LuaJIT headers missing' >&2; exit 1; }
if [[ "$(uname -s)" == Linux ]]; then
  [[ -f "$prefix/lib/libluajit-5.1.so" ]] || { echo 'LuaJIT Linux shared library missing' >&2; exit 1; }
else
  [[ -f "$prefix/lib/libluajit-5.1.dylib" ]] || { echo 'LuaJIT macOS shared library missing' >&2; exit 1; }
fi
"$work/src/luajit" -e 'assert(jit.status()); assert(type(table.clone)=="function"); assert(not pcall(select,"sandbox.bypass")); print("ArenaMP vendored Rubic0n LuaJIT:", jit.version, jit.os, jit.arch)'
printf 'Rubic0n LuaJIT installed to %s\n' "$prefix"
