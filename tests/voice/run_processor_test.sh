#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
speex="$root/extern/raknet/DependentExtensions/speex-1.1.12"
if [ ! -f "$speex/libspeex/preprocess.c" ] && [ -n "${RUNNER_TEMP:-}" ]; then
    candidate="$RUNNER_TEMP/CrabNet/DependentExtensions/speex-1.1.12"
    if [ -f "$candidate/libspeex/preprocess.c" ]; then
        speex="$candidate"
    fi
fi
if [ ! -f "$speex/libspeex/preprocess.c" ]; then
    echo "Speex DSP sources not found (expected CrabNet DependentExtensions/speex-1.1.12)" >&2
    exit 2
fi
for unit in preprocess mdf smallft fftwrap kiss_fft kiss_fftr misc; do
    "${CC:-cc}" -O2 -DFLOATING_POINT -I"$speex/include" -c "$speex/libspeex/$unit.c" -o "$build/$unit.o"
done
"${CXX:-c++}" -std=c++17 -O2 -I"$speex/include" -I"$root/apps/openmw/mwmp" \
    "$root/apps/openmw/mwmp/VoiceProcessor.cpp" "$root/tests/voice/voice_processor_test.cpp" \
    "$build"/*.o -pthread -lm -o "$build/check"
"$build/check"
