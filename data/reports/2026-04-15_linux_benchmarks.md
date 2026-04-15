# Benchmark Report — 2026-04-15 linux

**Generated:** 2026-04-15T08:02:12Z (UTC)  
**System:** Linux 6.17.0-22-generic  
**Python:** Python 3.13.7  
**Command used:** `./bench.sh "/home/zb_bamboo/DEV/images/Shaanxi" "5"`

---

## Summary

This report compares two builds of the `photo_indexer` binary:
- **avx2**: built with AVX2 optimizations enabled  
- **fallback**: built without AVX2 optimizations

The benchmark ran **5 measured iterations** per build, after 1 warmup run(s). Times are elapsed wall-clock seconds measured with `/usr/bin/time -f "%e"`.

**Number of photos scanned:** 2076

---

## Results

| Build | Mean (s) | Median (s) | StdDev (s) | Min (s) | Max (s) | Runs (s) |
|---|---:|---:|---:|---:|---:|---|
| **avx2** | 1.124 | 1.120 | 0.010 | 1.110 | 1.140 | 1.11 1.14 1.13 1.12 1.12 |
| **fallback** | 1.120 | 1.130 | 0.013 | 1.100 | 1.130 | 1.13 1.10 1.11 1.13 1.13 |

---

## Interpretation

The table above shows central tendency and dispersion for each build. A lower mean and median indicate faster overall runs; lower standard deviation indicates more consistent performance. Use these numbers to decide whether AVX2 optimizations provide a meaningful benefit for your workload and environment. Consider increasing iteration counts and using CPU pinning for more stable comparisons.

---

## Notes and next steps

- For more stable results, increase the number of iterations and consider CPU pinning via `TASKSET_CPU`.  
- I/O caching affects the first runs; the script performs a warmup run to reduce cold-cache effects.  
- Provide a CSV path as the third argument to persist raw run data.

---

*Report generated on 2026-04-15T08:02:12Z (UTC)*
