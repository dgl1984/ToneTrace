#!/bin/bash
# Build, test, stage and package ToneTrace for macOS (universal AU, VST3 and CLAP).
# Signing is ad hoc unless Developer ID variables are set; see scripts/package_macos.py.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build-macos"
LOG="$ROOT/BUILD_MACOS_LOG.txt"
JUCE="${JUCE_DIR:-$ROOT/deps/JUCE}"
CLAP_EXT="${CLAP_JUCE_EXTENSIONS_DIR:-$ROOT/deps/clap-juce-extensions}"
JOBS="$(sysctl -n hw.ncpu)"

exec > >(tee "$LOG") 2>&1
trap 'echo "=== FAIL ==="; echo "Build log: $LOG"' ERR

fail() { echo "ERROR: $*"; exit 1; }

VERSION="$(sed -nE 's/^[[:space:]]*([0-9]+\.[0-9]+\.[0-9]+)[[:space:]]*$/\1/p' "$ROOT/VERSION")"
[ -n "$VERSION" ] || fail "Could not read the ToneTrace version from VERSION."
echo "=== ToneTrace $VERSION - macOS AU + VST3 + CLAP Build and Validation ==="
echo "Source: $ROOT"

command -v cmake >/dev/null || fail "CMake is required and was not found on PATH."
command -v xcrun >/dev/null || fail "Xcode command-line tools are required: xcode-select --install"

PYTHON="$(command -v python3 || true)"
[ -n "$PYTHON" ] || fail "Python 3 is required and was not found on PATH."

if [ -n "${JUCE_DIR:-}" ] && [ ! -f "$JUCE/CMakeLists.txt" ]; then
    fail "JUCE_DIR does not contain CMakeLists.txt: $JUCE"
fi
if [ -n "${CLAP_JUCE_EXTENSIONS_DIR:-}" ] && [ ! -f "$CLAP_EXT/CMakeLists.txt" ]; then
    fail "CLAP_JUCE_EXTENSIONS_DIR does not contain CMakeLists.txt: $CLAP_EXT"
fi
if [ -z "${JUCE_DIR:-}" ] || [ -z "${CLAP_JUCE_EXTENSIONS_DIR:-}" ]; then
    echo "=== Verifying pinned JUCE 8.0.12 and CLAP dependencies ==="
    "$PYTHON" "$ROOT/scripts/fetch_deps.py"
fi

echo "Build directory: $BUILD"

echo "=== Configure universal Release build ==="
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DTONETRACE_BUILD_TOOLS=OFF \
    -DTONETRACE_BUILD_TESTS=ON \
    -DTONETRACE_BUILD_PLUGINS=ON \
    "-DJUCE_DIR=$JUCE" \
    "-DCLAP_JUCE_EXTENSIONS_DIR=$CLAP_EXT" \
    "-DPython3_EXECUTABLE=$PYTHON"

echo "=== Build all test targets and plug-ins ==="
cmake --build "$BUILD" --parallel "$JOBS" --target \
    tonetrace-tests \
    tonetrace-realtime-tests \
    tonetrace-ui-layout-tests \
    ToneTraceProductCatalogTests \
    ToneTraceCaptureModelTests \
    ToneTraceReleasedEngineTests \
    ToneTraceReleasedRealtimeTests \
    ToneTraceCorrectionControllerTests \
    ToneTraceNotificationToneTests \
    ToneTraceOutputBypassTests \
    ToneTraceBandPageLayoutTests \
    ToneTraceKeyboardControlTests \
    ToneTraceProcessorStateTests \
    tonetrace_clap_tests \
    ToneTraceEQ_AU \
    ToneTraceEQ_VST3 \
    ToneTraceEQ_CLAP

echo "=== Run automated tests ==="
ctest --test-dir "$BUILD" --output-on-failure

ARTEFACTS="$BUILD/plugins/juce/ToneTraceEQ_artefacts/Release"
for bundle in "AU/Tone Trace EQ.component" "VST3/Tone Trace EQ.vst3" "CLAP/Tone Trace EQ.clap"; do
    [ -d "$ARTEFACTS/$bundle" ] || fail "Build passed but $bundle could not be located."
done

echo "=== Install licenses for packaging ==="
INSTALL_STAGE="$BUILD/install-stage"
cmake --install "$BUILD" --config Release --component Release --prefix "$INSTALL_STAGE"

echo "=== Stage release tree ==="
PORTABLE="$BUILD/portable/ToneTrace"
"$PYTHON" "$ROOT/scripts/stage_release.py" --source-root "$ROOT" \
    --vst3 "$ARTEFACTS/VST3/Tone Trace EQ.vst3" \
    --clap "$ARTEFACTS/CLAP/Tone Trace EQ.clap" \
    --au "$ARTEFACTS/AU/Tone Trace EQ.component" \
    --licenses "$INSTALL_STAGE/licenses" \
    --output "$PORTABLE" --allowed-output-root "$BUILD"

echo "=== Sign, package installer and disk image ==="
"$PYTHON" "$ROOT/scripts/package_macos.py" --portable "$PORTABLE" --output-dir "$BUILD/release" --version "$VERSION"

echo "=== PASS ==="
echo "Disk image: $BUILD/release/ToneTrace_EQ_${VERSION}_macOS_Universal.dmg"
echo "Staged release tree: $PORTABLE"
echo "Build log: $LOG"
echo "Nothing was installed automatically."
