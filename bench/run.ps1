# bench/run.ps1 — Benchmark: Z vs C vs Rust vs Python (prime-counting algorithm)
# Usage (PowerShell): .\run.ps1 [N]      (default N=500000)
#
# Requirements:
#   zc.exe   — built with: cmake -B build && cmake --build build
#              OR: mingw32-make (MSYS2/MinGW)
#   clang    — from https://github.com/llvm/llvm-project/releases
#   gcc      — from MSYS2 (pacman -S mingw-w64-x86_64-gcc)
#   rustc    — from https://rustup.rs  (optional)
#   python3  — from https://python.org (optional)

param(
    [int]$N    = 500000,
    [int]$Reps = 20
)

$ErrorActionPreference = 'Continue'

$DIR  = Split-Path -Parent $MyInvocation.MyCommand.Path
$ROOT = Split-Path -Parent $DIR

$Z_SRC  = Join-Path $ROOT  "examples\primes.z"
$ZC_BIN = Join-Path $ROOT  "zc.exe"
$C_SRC  = Join-Path $DIR   "prime.c"
$PY_SRC = Join-Path $DIR   "prime.py"
$RS_SRC = Join-Path $DIR   "prime.rs"

$TEMP_DIR = $env:TEMP
$Z_BIN  = Join-Path $TEMP_DIR "_prime_z_bench.exe"
$C_BIN  = Join-Path $TEMP_DIR "_prime_c_bench.exe"
$RS_BIN = Join-Path $TEMP_DIR "_prime_rs_bench.exe"

# ── Helper: measure average wall time of a command ───────────────────
function Avg-Time {
    param([scriptblock]$Cmd, [int]$Reps)
    $total = 0.0
    for ($i = 0; $i -lt $Reps; $i++) {
        $ms = (Measure-Command { & $Cmd | Out-Null }).TotalMilliseconds
        $total += $ms
    }
    return [math]::Round($total / $Reps / 1000, 4)
}

# ── Header ───────────────────────────────────────────────────────────
Write-Host ""
Write-Host ("=" * 58)
Write-Host ("  BENCHMARK: Primes up to N={0,-14}  ({1} runs)" -f $N, $Reps)
Write-Host ("=" * 58)
Write-Host ""

# ── [1/4] Z (zc → clang -O3) ─────────────────────────────────────────
Write-Host "[1/4] Z  —  zc (LLVM backend, -O3)" -ForegroundColor Yellow
$Z_TIME = "N/A"; $Z_OUT = "(error)"

if (-not (Test-Path $ZC_BIN)) {
    Write-Host "  x  zc.exe not found. Build first:" -ForegroundColor Red
    Write-Host "     cmake -B build && cmake --build build --config Release"
} elseif (-not (Test-Path $Z_SRC)) {
    Write-Host "  x  primes.z not found at: $Z_SRC" -ForegroundColor Red
} else {
    Write-Host "  Compiling primes.z with zc..." -NoNewline
    & $ZC_BIN $Z_SRC -o $Z_BIN 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) {
        Write-Host " OK" -ForegroundColor Green
        $Z_OUT  = & $Z_BIN
        $Z_TIME = Avg-Time { & $Z_BIN } $Reps
        Write-Host "  v  $Z_OUT" -ForegroundColor Green
        Write-Host "     Average time: ${Z_TIME}s" -ForegroundColor Cyan
        Remove-Item $Z_BIN -ErrorAction SilentlyContinue
    } else {
        Write-Host " FAILED" -ForegroundColor Red
    }
}
Write-Host ""

# ── [2/4] C (gcc -O2) ────────────────────────────────────────────────
Write-Host "[2/4] C  —  gcc -O2" -ForegroundColor Yellow
$C_TIME = "N/A"; $C_OUT = "(error)"

if (-not (Test-Path $C_SRC)) {
    Write-Host "  x  prime.c not found." -ForegroundColor Red
} elseif (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
    Write-Host "  x  gcc not found in PATH. Install via MSYS2: pacman -S mingw-w64-x86_64-gcc" -ForegroundColor Red
} else {
    Write-Host "  Compiling with gcc -O2..." -NoNewline
    gcc -O2 -o $C_BIN $C_SRC 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) {
        Write-Host " OK" -ForegroundColor Green
        $C_OUT  = & $C_BIN --n $N
        $C_TIME = Avg-Time { & $C_BIN --n $N } $Reps
        Write-Host "  v  $C_OUT" -ForegroundColor Green
        Write-Host "     Average time: ${C_TIME}s" -ForegroundColor Cyan
        Remove-Item $C_BIN -ErrorAction SilentlyContinue
    } else {
        Write-Host " FAILED" -ForegroundColor Red
    }
}
Write-Host ""

# ── [3/4] Rust (rustc opt-level=3) ───────────────────────────────────
Write-Host "[3/4] Rust  —  rustc -C opt-level=3" -ForegroundColor Yellow
$RS_TIME = "N/A"; $RS_OUT = "(error)"

if (-not (Test-Path $RS_SRC)) {
    Write-Host "  x  prime.rs not found." -ForegroundColor Red
} elseif (-not (Get-Command rustc -ErrorAction SilentlyContinue)) {
    Write-Host "  x  rustc not found. Install from https://rustup.rs" -ForegroundColor Red
} else {
    Write-Host "  Compiling with rustc -C opt-level=3..." -NoNewline
    rustc -C opt-level=3 -o $RS_BIN $RS_SRC 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) {
        Write-Host " OK" -ForegroundColor Green
        $RS_OUT  = & $RS_BIN --n $N
        $RS_TIME = Avg-Time { & $RS_BIN --n $N } $Reps
        Write-Host "  v  $RS_OUT" -ForegroundColor Green
        Write-Host "     Average time: ${RS_TIME}s" -ForegroundColor Cyan
        Remove-Item $RS_BIN -ErrorAction SilentlyContinue
    } else {
        Write-Host " FAILED" -ForegroundColor Red
    }
}
Write-Host ""

# ── [4/4] Python 3 ───────────────────────────────────────────────────
Write-Host "[4/4] Python 3" -ForegroundColor Yellow
$PY_TIME = "N/A"; $PY_OUT = "(error)"

$PY_CMD = if (Get-Command python3 -ErrorAction SilentlyContinue) { "python3" }
          elseif (Get-Command python -ErrorAction SilentlyContinue) { "python" }
          else { $null }

if (-not (Test-Path $PY_SRC)) {
    Write-Host "  x  prime.py not found." -ForegroundColor Red
} elseif (-not $PY_CMD) {
    Write-Host "  x  python/python3 not found in PATH." -ForegroundColor Red
} else {
    $PY_OUT  = & $PY_CMD $PY_SRC --n $N
    $PY_TIME = Avg-Time { & $PY_CMD $PY_SRC --n $N } $Reps
    Write-Host "  v  $PY_OUT" -ForegroundColor Green
    Write-Host "     Average time: ${PY_TIME}s" -ForegroundColor Cyan
}
Write-Host ""

# ── Summary table ─────────────────────────────────────────────────────
Write-Host ("=" * 58)
Write-Host "  SUMMARY"
Write-Host ("=" * 58)
Write-Host ("  {0,-20}  {1,8} s   {2}" -f "Z  (zc→clang-O3)",  $Z_TIME,  "native binary")
Write-Host ("  {0,-20}  {1,8} s   {2}" -f "C  (gcc -O2)",       $C_TIME,  "native binary")
Write-Host ("  {0,-20}  {1,8} s   {2}" -f "Rust (opt-level=3)", $RS_TIME, "native binary")
Write-Host ("  {0,-20}  {1,8} s   {2}" -f "Python 3",           $PY_TIME, "CPython")
Write-Host ("=" * 58)

if ($Z_TIME -ne "N/A") {
    $zf = [double]$Z_TIME
    foreach ($pair in @(
        @("C (gcc -O2)",        $C_TIME),
        @("Rust (opt-level=3)", $RS_TIME),
        @("Python 3",           $PY_TIME)
    )) {
        $label = $pair[0]; $tstr = $pair[1]
        if ($tstr -ne "N/A" -and $zf -gt 0) {
            $tf = [double]$tstr
            $r  = [math]::Round($tf / $zf, 2)
            if ([math]::Abs($r - 1) -lt 0.05) {
                Write-Host ("  Z ≈ {0} (within 5%)" -f $label)
            } elseif ($r -gt 1) {
                Write-Host ("  Z is {0}x faster than {1}" -f $r, $label)
            } else {
                Write-Host ("  Z is {0}x slower than {1}" -f [math]::Round(1/$r,2), $label)
            }
        }
    }
}
Write-Host ("  Average of {0} runs per language" -f $Reps)
Write-Host ("=" * 58)
Write-Host ""
