#!/usr/bin/env bash
# bench/run.sh — Benchmark: Z vs C vs Rust vs Python (prime-counting algorithm)
# Usage: ./run.sh [N]    (default N=500000)
#
# Runs each program REPS times and reports the average wall time per run.
# The zc compiler must be built first (run `make` in the project root).

set -euo pipefail

N="${1:-500000}"
REPS=20
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/.." && pwd)"

C_SRC="$DIR/prime.c"
PY_SRC="$DIR/prime.py"
RS_SRC="$DIR/prime.rs"
Z_SRC="$ROOT/examples/primes.z"
ZC_BIN="$ROOT/zc"
Z_BIN="$DIR/_prime_z_bench_tmp"
C_BIN="$DIR/_prime_c_bench_tmp"
RS_BIN="$DIR/_prime_rs_bench_tmp"

# ── ANSI colours ─────────────────────────────────────────────────────
BOLD=$'\033[1m'; NC=$'\033[0m'
GREEN=$'\033[0;32m'; YELLOW=$'\033[1;33m'
CYAN=$'\033[0;36m';  MAGENTA=$'\033[0;35m'
GRAY=$'\033[0;90m';  RED=$'\033[0;31m'

# ── Timing helper ────────────────────────────────────────────────────
avg_time() {
    local _var="$1"; shift
    local _t
    _t=$(python3 - "$@" <<PYEOF
import subprocess, time, sys
cmd = sys.argv[1:]
reps = $REPS
t0 = time.perf_counter()
for _ in range(reps):
    subprocess.run(cmd, capture_output=True)
t1 = time.perf_counter()
print(f"{(t1 - t0) / reps:.4f}")
PYEOF
)
    eval "$_var=\$_t"
}

# ── Header ───────────────────────────────────────────────────────────
echo
printf '%s\n' "${BOLD}╔══════════════════════════════════════════════════════╗${NC}"
printf "${BOLD}║   BENCHMARK: Primes up to N=%-22s ║${NC}\n" "$N"
printf '%s\n' "${BOLD}║   (average of $REPS runs per language)                  ║${NC}"
printf '%s\n' "${BOLD}╚══════════════════════════════════════════════════════╝${NC}"
echo

# ─────────────────────────────────────────────────────────────────────
# [1/4]  Z — zc compiler (Z → LLVM IR → clang -O3 → native binary)
# ─────────────────────────────────────────────────────────────────────
printf '%s\n' "${YELLOW}${BOLD}[1/4] Z  —  zc (LLVM backend, -O3)${NC}"

Z_TIME="N/A"; Z_OUT="(error)"
if [[ ! -f "$ZC_BIN" ]]; then
    printf '  %s\n' "${RED}✗ zc not found. Run 'make' first.${NC}"
elif [[ ! -f "$Z_SRC" ]]; then
    printf '  %s\n' "${RED}✗ primes.z not found.${NC}"
else
    printf '  Compiling primes.z with zc... '
    "$ZC_BIN" "$Z_SRC" -o "$Z_BIN" >/dev/null 2>&1
    printf '%s\n' "${GREEN}OK${NC}"
    Z_OUT=$("$Z_BIN")
    avg_time Z_TIME "$Z_BIN"
    printf '  %s\n' "${GREEN}✓ $Z_OUT${NC}"
    printf '  %s\n' "${CYAN}  Average time: ${Z_TIME}s${NC}"
    rm -f "$Z_BIN"
fi
echo

# ─────────────────────────────────────────────────────────────────────
# [2/4]  C — gcc -O2
# ─────────────────────────────────────────────────────────────────────
printf '%s\n' "${YELLOW}${BOLD}[2/4] C  —  gcc -O2${NC}"

C_TIME="N/A"; C_OUT="(error)"
if [[ ! -f "$C_SRC" ]]; then
    printf '  %s\n' "${RED}✗ prime.c not found.${NC}"
else
    printf '  Compiling with gcc -O2... '
    gcc -O2 -o "$C_BIN" "$C_SRC"
    printf '%s\n' "${GREEN}OK${NC}"
    C_OUT=$("$C_BIN" --n "$N")
    avg_time C_TIME "$C_BIN" --n "$N"
    printf '  %s\n' "${GREEN}✓ $C_OUT${NC}"
    printf '  %s\n' "${CYAN}  Average time: ${C_TIME}s${NC}"
    rm -f "$C_BIN"
fi
echo

# ─────────────────────────────────────────────────────────────────────
# [3/4]  Rust — rustc -C opt-level=3
# ─────────────────────────────────────────────────────────────────────
printf '%s\n' "${YELLOW}${BOLD}[3/4] Rust  —  rustc -C opt-level=3${NC}"

RS_TIME="N/A"; RS_OUT="(error)"
if [[ ! -f "$RS_SRC" ]]; then
    printf '  %s\n' "${RED}✗ prime.rs not found.${NC}"
elif ! command -v rustc &>/dev/null; then
    printf '  %s\n' "${RED}✗ rustc not found in PATH.${NC}"
else
    printf '  Compiling with rustc -C opt-level=3... '
    rustc -C opt-level=3 -o "$RS_BIN" "$RS_SRC" 2>/dev/null
    printf '%s\n' "${GREEN}OK${NC}"
    RS_OUT=$("$RS_BIN" --n "$N")
    avg_time RS_TIME "$RS_BIN" --n "$N"
    printf '  %s\n' "${GREEN}✓ $RS_OUT${NC}"
    printf '  %s\n' "${CYAN}  Average time: ${RS_TIME}s${NC}"
    rm -f "$RS_BIN"
fi
echo

# ─────────────────────────────────────────────────────────────────────
# [4/4]  Python 3
# ─────────────────────────────────────────────────────────────────────
printf '%s\n' "${YELLOW}${BOLD}[4/4] Python 3${NC}"

PY_TIME="N/A"; PY_OUT="(error)"
if [[ ! -f "$PY_SRC" ]]; then
    printf '  %s\n' "${RED}✗ prime.py not found.${NC}"
else
    PY_OUT=$(python3 "$PY_SRC" --n "$N")
    avg_time PY_TIME python3 "$PY_SRC" --n "$N"
    printf '  %s\n' "${GREEN}✓ $PY_OUT${NC}"
    printf '  %s\n' "${CYAN}  Average time: ${PY_TIME}s${NC}"
fi
echo

# ─────────────────────────────────────────────────────────────────────
# Summary table
# ─────────────────────────────────────────────────────────────────────
printf '%s\n' "${BOLD}╔═══════════════════════════════════════════════════════════╗${NC}"
printf '%s\n' "${BOLD}║                       SUMMARY                            ║${NC}"
printf '%s\n' "${BOLD}╠═══════════════════════════════════════════════════════════╣${NC}"
printf "${BOLD}║${NC}  ${MAGENTA}%-17s${NC}  %7s s   %-23s${BOLD}║${NC}\n" \
    "Z  (zc→clang-O3)"  "$Z_TIME"  "native binary"
printf "${BOLD}║${NC}  ${GREEN}%-17s${NC}  %7s s   %-23s${BOLD}║${NC}\n" \
    "C  (gcc -O2)"       "$C_TIME"  "native binary"
printf "${BOLD}║${NC}  ${YELLOW}%-17s${NC}  %7s s   %-23s${BOLD}║${NC}\n" \
    "Rust (opt-level=3)" "$RS_TIME" "native binary"
printf "${BOLD}║${NC}  ${CYAN}%-17s${NC}  %7s s   %-23s${BOLD}║${NC}\n" \
    "Python 3"           "$PY_TIME" "CPython interpreter"
printf '%s\n' "${BOLD}╠═══════════════════════════════════════════════════════════╣${NC}"

# Relative speeds
if [[ "$Z_TIME" != "N/A" && "$PY_TIME" != "N/A" ]]; then
    python3 -c "
z   = float('$Z_TIME')
c   = float('$C_TIME')   if '$C_TIME'  != 'N/A' else None
rs  = float('$RS_TIME')  if '$RS_TIME' != 'N/A' else None
py  = float('$PY_TIME')

bold  = '\033[1m'; nc = '\033[0m'

def row(label, t):
    if t and t > 0 and z > 0:
        r = t / z
        if abs(r - 1) < 0.05:
            print(f'{bold}║{nc}  Z ≈ {label} (within 5%)                 {bold}║{nc}')
        elif r > 1:
            print(f'{bold}║{nc}  Z is {r:.2f}x faster than {label:<14} {bold}║{nc}')
        else:
            print(f'{bold}║{nc}  Z is {1/r:.2f}x slower  than {label:<14} {bold}║{nc}')

row('C (gcc -O2)',        c)
row('Rust (opt-level=3)', rs)
row('Python 3',           py)
"
fi

printf '%s\n' "${BOLD}║${NC}  Average of ${REPS} runs per language                      ${BOLD}║${NC}"
printf '%s\n' "${BOLD}╚═══════════════════════════════════════════════════════════╝${NC}"
echo
