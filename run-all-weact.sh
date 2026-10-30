#!/usr/bin/env bash
# run-all-weact.sh — build, flash and run every example on a WeAct F411CE, one by one.
#
#   ./run-all-weact.sh                 # all examples
#   ./run-all-weact.sh float           # only examples whose name matches "float"
#   DRY_RUN=1 ./run-all-weact.sh       # list what would run
#
# A test passes when its completion marker appears and no failure pattern does.
# It stops as soon as either is seen. Failures do not stop the run.
#
# Tunables:
#   RUN_MS=240000  OpenOCD run window per firmware (default 240 s)
#   WALL_S=600     hard cap per example, including build time (default 600 s)
#   GAP=3          seconds between examples
#   LOGDIR=...     log directory (default logs/weact-<timestamp>)
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export RUN_MS="${RUN_MS:-240000}"
WALL_S="${WALL_S:-600}"
GAP="${GAP:-3}"
FILTER="${1:-}"
LOGDIR="${LOGDIR:-$ROOT/logs/weact-$(date +%Y%m%d-%H%M%S)}"
FAIL_RE='LOAD FAILED|RESULT: FAILURE|FAIL:|dlopen failed|HardFault|Hard Fault|Error: |error: |\*\*\* \['

# name | completion marker
EXAMPLES=(
  "float-reloc|float-reloc: done"
  "float-static|float-static: done"
  "float-static-full-ram|float-static-full-ram: done"
  "float-optimal|floatapp: done"
  "float-dynamic|RESULT: SUCCESS"
  "float-dynamic-xip|RESULT: SUCCESS"
  "float-overlay|RESULT: SUCCESS"
  "spifs-reloc|spifs-reloc: done"
  "spifs-static|spifs-static: done"
  "spi-static-full-ram|spi-static-full-ram: done"
  "spifs-optimal|spifs-optimal: done"
  "spifs-dynamic|RESULT: SUCCESS"
  "spifs-dynamic-xip|RESULT: SUCCESS"
  "spifs-overlay|RESULT: SUCCESS"
  "combo-reloc|combo-reloc: done"
  "combo-static|combo-static: done"
  "combo-static-full-ram|combo-static-full-ram: done"
  "combo-optimal|combo-optimal: done"
  "combo-dynamic|RESULT: SUCCESS"
  "combo-dynamic-xip|RESULT: SUCCESS"
  "combo-overlay|RESULT: SUCCESS"
  "api-static-boot-table|RESULT: SUCCESS"
  "api-optimal-boot-table|RESULT: SUCCESS"
  "api-dynamic-xip-semihost-table|RESULT: SUCCESS"
  "api-nolib-eager|RESULT: SUCCESS"
)

stop_openocd() { killall -s 9 openocd >/dev/null 2>&1 || true; }

declare -a RESULTS=()
pass=0; fail=0
[[ "${DRY_RUN:-0}" == "1" ]] || mkdir -p "$LOGDIR"

for entry in "${EXAMPLES[@]}"; do
    IFS='|' read -r name marker <<<"$entry"
    [[ -n "$FILTER" && ! "$name" =~ $FILTER ]] && continue
    [[ -x "$ROOT/$name/run.sh" ]] || continue
    if [[ "${DRY_RUN:-0}" == "1" ]]; then echo "$name  [$marker]"; continue; fi

    log="$LOGDIR/$name.log"
    echo "--- RUN  $name"
    stop_openocd
    ( cd "$ROOT/$name" && exec ./run.sh ) >"$log" 2>&1 &
    pid=$!

    status=FAIL; why="timeout after ${WALL_S}s"
    for ((t = 0; t < WALL_S; t++)); do
        if grep -qF -- "$marker" "$log"; then status=PASS; why=""; sleep 1; fi
        if grep -qE -- "$FAIL_RE" "$log"; then
            status=FAIL; why="$(grep -m1 -E -- "$FAIL_RE" "$log" | cut -c1-100)"; break
        fi
        [[ "$status" == "PASS" ]] && break
        if ! kill -0 "$pid" 2>/dev/null; then
            grep -qF -- "$marker" "$log" && { status=PASS; why=""; } || why="exited without marker"
            break
        fi
        sleep 1
    done
    stop_openocd
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true

    echo "--- $status $name ${why:+($why)}"
    RESULTS+=("$status|$name|$why")
    if [[ "$status" == "PASS" ]]; then ((pass++)); else ((fail++)); fi
    sleep "$GAP"
done

[[ "${DRY_RUN:-0}" == "1" ]] && exit 0
echo
echo "================ SUMMARY ================"
for r in "${RESULTS[@]}"; do
    IFS='|' read -r st name why <<<"$r"
    printf '%-5s %-32s %s\n' "$st" "$name" "$why"
done
echo "-----------------------------------------"
echo "PASS=$pass  FAIL=$fail"
echo "logs: $LOGDIR"
[[ $fail -eq 0 ]]
