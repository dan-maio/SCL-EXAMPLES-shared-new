#!/usr/bin/env bash
# run-all-lyra.sh — build and run every example on the Luckfox Lyra B, one by one,
# showing the semihosting console live, with a gap between examples.
#
#   ./run-all-lyra.sh            # all examples
#   ./run-all-lyra.sh xip        # only examples whose name matches "xip"
#
# Uses the port's OpenOCD sequence (test/boards/luckfox-lyra/hw.sh), which does a
# full RK3506 global reset before each run. The UART is not opened, so it stays
# free for a terminal; the firmware prints the same results there.
#
# Tunables: GAP (seconds, default 3), SECS (per example, default 180), UOS_PORT_DIR, BUILD.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UOS_PORT_DIR="${UOS_PORT_DIR:-$HERE/../uos/micro-os-plus-iii-aarch32}"
BUILD="${BUILD:-$HERE/build}"
SECS="${SECS:-180}"
GAP="${GAP:-3}"
FILTER="${1:-}"
LOGDIR="$HERE/logs/lyra-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$LOGDIR"
ALL=(reloc static optimal dynamic-newlib dynamic-nolib xip-static xip-dynamic-nolib xip-dynamic-newlib)
EXAMPLES=()
for app in float sd combo; do for l in "${ALL[@]}" overlay; do EXAMPLES+=("$app-$l"); done; done
for app in cpp; do
    for l in "${ALL[@]}"; do
        [[ "$app-$l" == cpp-static ]] || EXAMPLES+=("$app-$l")   # scl::static has no cyclic imports
    done
done
for app in provcons slib drivers cppeh; do
    for l in "${ALL[@]}"; do EXAMPLES+=("$app-$l"); done
done
EXAMPLES+=(ovfix-overlay)
pass=0; fail=0
declare -a RESULTS=()
export PATH="${ARM_TOOLCHAIN_BIN:-/home/dan/.local/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin}:$PATH"
[[ -e "$HERE/SCL-shared/CMakeLists.txt" ]] || git -C "$HERE/.." submodule update --init luckfox-lyra/SCL-shared
cmake -S "$HERE" -B "$BUILD" >/dev/null
cmake --build "$BUILD" --target examples -j8 >"$LOGDIR/build.log" 2>&1 || { echo "build failed: $LOGDIR/build.log"; exit 1; }
for t in "${EXAMPLES[@]}"; do
    [[ -n "$FILTER" && ! "$t" =~ $FILTER ]] && continue
    echo
    echo "================================================================"
    echo "--- RUN  $t"
    echo "================================================================"
    # OpenOCD block-buffers a pipe; line-buffer it so the console is live.
    BUILD="$BUILD" OPENOCD="$HERE/openocd-linebuf.sh" \
        "$UOS_PORT_DIR/test/boards/luckfox-lyra/hw.sh" "$t" "$SECS" 2>&1 | tee "$LOGDIR/$t.log"
    rc=${PIPESTATUS[0]}
    if [[ $rc -eq 0 ]]; then
        echo "--- PASS $t"; RESULTS+=("PASS  $t"); ((pass++))
    else
        echo "--- FAIL $t (rc=$rc)"; RESULTS+=("FAIL  $t"); ((fail++))
    fi
    echo "--- gap ${GAP}s"
    sleep "$GAP"
done
echo
echo "================ SUMMARY ================"
printf '%s\n' "${RESULTS[@]}"
echo "PASS=$pass  FAIL=$fail"
echo "logs: $LOGDIR"
[[ $fail -eq 0 ]]
