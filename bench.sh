#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

# USAGE:
#   chmod +x bench.sh
#   ./bench.sh /path/to/sample_images [iterations] [csv_output]
#
# Example:
#   ./bench.sh ./sample_images 5 results.csv
#
# Optional environment:
#   TASKSET_CPU="0-3"   # if set, will pin runs to these CPUs via taskset
#
# This script:
# - builds two variants (avx2 / fallback) via Conan + CMake
# - runs warmup + measured iterations
# - computes mean, median, stddev, min, max (POSIX tools only)
# - writes a Markdown report to data/reports/<YYYY-MM-DD>_<os>_benchmarks.md (overwrites)
# - optionally appends CSV results to the provided CSV_OUT
#
# Requirements:
# - conan, cmake, /usr/bin/time (or time in PATH), taskset optional
# - built binary expected to be named `photo_indexer` (or in Release/)

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/build_bench"
SAMPLE_DIR="${1:-./sample_images}"
ITERATIONS="${2:-5}"
CSV_OUT="${3:-}"   # optional CSV file path

OUT_PREFIX="bench_gallery"
WARMUP=1            # number of warmup runs (not measured)
MAKE_PARALLEL=1     # adjust if needed

REPORT_DIR="$PROJECT_ROOT/data/reports"
DATE_STR="$(date -I)"                       # YYYY-MM-DD
OS_STR="$(uname -s | tr '[:upper:]' '[:lower:]')"  # e.g., linux, darwin
REPORT_FILE="$REPORT_DIR/${DATE_STR}_${OS_STR}_benchmarks.md"
GEN_TS="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"   # UTC ISO timestamp
PYTHON_VER="$(python3 --version 2>/dev/null || echo 'python3 not found')"

if [ ! -d "$SAMPLE_DIR" ]; then
  echo "Sample folder not found: $SAMPLE_DIR"
  exit 1
fi

# Count photos in SAMPLE_DIR (extensions matched by the indexer)
NUM_PHOTOS=$(find "$SAMPLE_DIR" -type f \( -iname '*.jpg' -o -iname '*.jpeg' -o -iname '*.png' -o -iname '*.tiff' -o -iname '*.tif' -o -iname '*.avif' \) | wc -l | tr -d ' ')
if [ -z "$NUM_PHOTOS" ]; then
  NUM_PHOTOS=0
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$REPORT_DIR"

# Detect time command
if [ -x "/usr/bin/time" ]; then
  TIME_CMD="/usr/bin/time"
elif command -v time >/dev/null 2>&1; then
  TIME_CMD="$(command -v time)"
else
  echo "Error: 'time' command not found. Install 'time' (usually package 'time')."
  exit 1
fi

# If TASKSET_CPU is set, ensure taskset exists; otherwise ignore
if [ -n "${TASKSET_CPU:-}" ] && ! command -v taskset >/dev/null 2>&1; then
  echo "Warning: TASKSET_CPU is set but 'taskset' not found. Ignoring TASKSET_CPU."
  unset TASKSET_CPU
fi

# compute stats (mean, median, stddev, min, max) using POSIX tools
# reads newline-separated numbers from stdin, prints: mean median stddev min max
compute_stats_awk() {
  local tmp
  tmp="$(mktemp /tmp/bench_times.XXXXXX)" || exit 1
  cat > "$tmp"

  local n
  n=$(wc -l < "$tmp" | tr -d ' ')
  if [ "$n" -eq 0 ]; then
    echo "0 0 0 0 0"
    rm -f "$tmp"
    return
  fi

  # mean, stddev (population), min, max
  read mean stddev minv maxv < <(
    awk '
    {
      x = $1 + 0
      sum += x
      sumsq += x*x
      if (NR==1 || x < min) min = x
      if (NR==1 || x > max) max = x
    }
    END {
      n = NR
      mean = sum / n
      variance = (sumsq / n) - (mean * mean)
      if (variance < 0) variance = 0
      stddev = sqrt(variance)
      printf("%.3f %.3f %.3f %.3f\n", mean, stddev, min, max)
    }' "$tmp"
  )

  # median via sort + awk
  median=$(sort -n "$tmp" | awk -v n="$n" '{
    a[NR] = $1
  }
  END {
    if (n % 2 == 1) {
      printf("%.3f", a[(n+1)/2])
    } else {
      printf("%.3f", (a[n/2] + a[n/2+1]) / 2)
    }
  }')

  printf "%s %s %s %s %s\n" "$mean" "$median" "$stddev" "$minv" "$maxv"
  rm -f "$tmp"
}

# Helper: find conan toolchain file under a directory
find_toolchain() {
  local dir="$1"
  find "$dir" -type f -name 'conan_toolchain.cmake' -print -quit || true
}

# Helper to configure, build and run
# Usage: build_and_run <name> <USE_AVX2: ON|OFF> <extra_cmake_flags...>
build_and_run() {
  local name="$1"
  local use_avx2="$2"
  shift 2
  local extra_flags=("$@")

  local workdir="$BUILD_DIR/$name"
  mkdir -p "$workdir"
  pushd "$workdir" > /dev/null

  echo "=== Conan install for $name ==="
  conan install "$PROJECT_ROOT" --output-folder="$workdir" --build=missing

  TOOLCHAIN_PATH="$(find_toolchain "$workdir")"
  if [ -z "$TOOLCHAIN_PATH" ]; then
    echo "Error: conan_toolchain.cmake not found under $workdir"
    echo "Debug listing (depth 3):"
    find "$workdir" -maxdepth 3 -type d -print
    popd > /dev/null
    exit 1
  fi
  echo "Using toolchain: $TOOLCHAIN_PATH"

  cmake_args=(
    "$PROJECT_ROOT"
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_PATH"
    -DUSE_AVX2="$use_avx2"
    -DCMAKE_BUILD_TYPE=Release
  )
  cmake_args+=("${extra_flags[@]}")

  echo "=== Configuring $name ==="
  cmake "${cmake_args[@]}"

  echo "=== Building $name ==="
  cmake --build . --config Release -- -j"${MAKE_PARALLEL}"

  # determine binary path (support Release/ subdir or direct)
  local bin="$workdir/photo_indexer"
  if [ ! -x "$bin" ]; then
    if [ -x "$workdir/Release/photo_indexer" ]; then
      bin="$workdir/Release/photo_indexer"
    elif [ -x "$workdir/photo_indexer.exe" ]; then
      bin="$workdir/photo_indexer.exe"
    fi
  fi
  if [ ! -x "$bin" ]; then
    echo "Error: built binary not found at expected locations:"
    echo "  $workdir/photo_indexer"
    echo "  $workdir/Release/photo_indexer"
    popd > /dev/null
    exit 1
  fi

  echo "=== Warmup run(s): $WARMUP ==="
  for ((w=1; w<=WARMUP; w++)); do
    if [ -n "${TASKSET_CPU:-}" ]; then
      taskset -c "$TASKSET_CPU" "$bin" "$SAMPLE_DIR" "$OUT_PREFIX" "$workdir/output" >/dev/null 2>&1 || true
    else
      "$bin" "$SAMPLE_DIR" "$OUT_PREFIX" "$workdir/output" >/dev/null 2>&1 || true
    fi
    rm -rf "$workdir/output"*
  done

  echo "=== Running $name ($ITERATIONS iterations) ==="
  local times=()
  for i in $(seq 1 "$ITERATIONS"); do
    echo "Run #$i..."
    local t
    if [ -n "${TASKSET_CPU:-}" ]; then
      t=$( { "$TIME_CMD" -f "%e" taskset -c "$TASKSET_CPU" "$bin" "$SAMPLE_DIR" "$OUT_PREFIX" "$workdir/output" 1>/dev/null; } 2>&1 | tail -n1 )
    else
      t=$( { "$TIME_CMD" -f "%e" "$bin" "$SAMPLE_DIR" "$OUT_PREFIX" "$workdir/output" 1>/dev/null; } 2>&1 | tail -n1 )
    fi
    if ! printf '%s' "$t" | grep -Eq '^[0-9]+([.][0-9]+)?$'; then
      echo "Error: failed to capture time (got: '$t')"
      popd > /dev/null
      exit 1
    fi
    echo "  time: ${t}s"
    times+=("$t")
    rm -rf "$workdir/output"*
  done

  # compute stats via compute_stats_awk
  local stats
  stats="$(printf "%s\n" "${times[@]}" | compute_stats_awk)"
  # stats: mean median stddev min max
  local mean median stddev minv maxv
  read -r mean median stddev minv maxv <<< "$stats"

  echo "Results for $name:"
  echo "  runs: ${times[*]}"
  echo "  mean: ${mean} s"
  echo "  median: ${median} s"
  echo "  stddev: ${stddev} s"
  echo "  min: ${minv} s"
  echo "  max: ${maxv} s"

  # optional CSV append
  if [ -n "$CSV_OUT" ]; then
    mkdir -p "$(dirname "$CSV_OUT")"
    if [ ! -f "$CSV_OUT" ]; then
      echo "mode,name,iterations,mean,median,stddev,min,max,runs" > "$CSV_OUT"
    fi
    local runs_joined
    runs_joined=$(IFS=';'; echo "${times[*]}")
    echo "bench,$name,$ITERATIONS,$mean,$median,$stddev,$minv,$maxv,\"$runs_joined\"" >> "$CSV_OUT"
    echo "Appended results to $CSV_OUT"
  fi

  # store results for report
  REPORT_RUNS["$name"]="${times[*]}"
  REPORT_MEAN["$name"]="$mean"
  REPORT_MEDIAN["$name"]="$median"
  REPORT_STDDEV["$name"]="$stddev"
  REPORT_MIN["$name"]="$minv"
  REPORT_MAX["$name"]="$maxv"

  popd > /dev/null
}

# declare associative arrays to collect results
declare -A REPORT_RUNS
declare -A REPORT_MEAN
declare -A REPORT_MEDIAN
declare -A REPORT_STDDEV
declare -A REPORT_MIN
declare -A REPORT_MAX

# Run AVX2 build and fallback build
build_and_run "avx2" "ON" -DCMAKE_CXX_FLAGS="-mavx2 -mfma -O3 -march=native -pthread"
build_and_run "fallback" "OFF" -DCMAKE_CXX_FLAGS="-O3 -pthread"

echo "Benchmark finished."

# -------------------------
# Write Markdown report
# -------------------------
# Build the exact command string used to run this script
if [ -n "$CSV_OUT" ]; then
  CMD_USED="./$(basename "$0") \"$SAMPLE_DIR\" \"$ITERATIONS\" \"$CSV_OUT\""
else
  CMD_USED="./$(basename "$0") \"$SAMPLE_DIR\" \"$ITERATIONS\""
fi

{
  cat > "$REPORT_FILE" <<EOF
# Benchmark Report — ${DATE_STR} ${OS_STR}

**Generated:** ${GEN_TS} (UTC)  
**System:** $(uname -s) $(uname -r)  
**Python:** ${PYTHON_VER}  
**Command used:** \`${CMD_USED}\`

---

## Summary

This report compares two builds of the \`photo_indexer\` binary:
- **avx2**: built with AVX2 optimizations enabled  
- **fallback**: built without AVX2 optimizations

The benchmark ran **${ITERATIONS} measured iterations** per build, after ${WARMUP} warmup run(s). Times are elapsed wall-clock seconds measured with \`${TIME_CMD} -f "%e"\`.

**Number of photos scanned:** ${NUM_PHOTOS}

---

## Results

| Build | Mean (s) | Median (s) | StdDev (s) | Min (s) | Max (s) | Runs (s) |
|---|---:|---:|---:|---:|---:|---|
| **avx2** | ${REPORT_MEAN[avx2]} | ${REPORT_MEDIAN[avx2]} | ${REPORT_STDDEV[avx2]} | ${REPORT_MIN[avx2]} | ${REPORT_MAX[avx2]} | ${REPORT_RUNS[avx2]} |
| **fallback** | ${REPORT_MEAN[fallback]} | ${REPORT_MEDIAN[fallback]} | ${REPORT_STDDEV[fallback]} | ${REPORT_MIN[fallback]} | ${REPORT_MAX[fallback]} | ${REPORT_RUNS[fallback]} |

---

## Interpretation

The table above shows central tendency and dispersion for each build. A lower mean and median indicate faster overall runs; lower standard deviation indicates more consistent performance. Use these numbers to decide whether AVX2 optimizations provide a meaningful benefit for your workload and environment. Consider increasing iteration counts and using CPU pinning for more stable comparisons.

---

## Notes and next steps

- For more stable results, increase the number of iterations and consider CPU pinning via \`TASKSET_CPU\`.  
- I/O caching affects the first runs; the script performs a warmup run to reduce cold-cache effects.  
- Provide a CSV path as the third argument to persist raw run data.

---

*Report generated on ${GEN_TS} (UTC)*
EOF
}

echo "Wrote report: $REPORT_FILE"
