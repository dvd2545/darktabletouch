# Touch editor performance on ROG Ally

Measurement dates: October 27-28, 2026.

## Workload and method

- ROG Ally, Ryzen Z1 Extreme, Radeon gfx1103, Windows, 12,273,524 KiB of
  visible physical memory. The existing Windows power scheme was Silent.
- MSYS2 UCRT64 Release build, with the same native engine and module
  algorithms for baseline and modified Touch controllers. Baseline Touch
  sources came from `e0581eaf8`; its temporary build target is not shipped.
- Ten CR3 files: five sorted files from each of `69-10-21` and
  `69-10-23 2` under the supplied photo root. Every run copied RAW/XMP
  files into an isolated temporary catalog. Rejected ratings were cleared
  only in those copied XMPs. Benchmarks disabled sidecar writes; native
  workflow tests separately exercised sidecar saving and recovery.
- Original histories and a second workload enabling AgX, lens correction,
  and profiled denoise. Module edits also checked that visible RGB pixels
  changed. The lens edit used the scale parameter for the active native
  correction method.
- Twenty alternating forward/reverse passes through all ten images,
  with 750 ms idle between operations. A separate forty-switch prepared
  pair started after five seconds idle. Each measured module received
  twenty committed parameter changes.
- Timers start at the controller request and end at frame publication.
  First-frame time includes a prepared frame; final-frame time waits for
  the target editor to become ready. These are software publication
  measurements, not touchscreen-to-display or compositor latency.
- Cold samples mean editor/frame-cache misses. Windows filesystem caches
  were not flushed. Startup and OpenCL kernel compilation are outside
  the recorded image-operation times. The first successful isolated AMD
  kernel-cache initialization took approximately 29 seconds; subsequent
  GPU measurements reused that validated cache.
- Peak working set includes native input buffers and other engine memory.
  The 2 GiB retained-cache limit is not a cap on total process memory.
- A draft thumbnail is used when it is already available with a current
  native history hash. A miss proceeds directly to the viewport render:
  an extra 640-pixel full-RAW pipeline repeated expensive upstream work.
  This differs from the proposed unconditional two-render cold path.

## Results

Times below are milliseconds, shown as median / nearest-rank p95. The
twenty-pass measurements used implementation `168ca1595`. These are measurements
on this machine and power profile, not general performance guarantees.

| Workload and operation | Baseline CPU | Touch CPU | Touch AMD OpenCL |
| --- | ---: | ---: | ---: |
| Original history, initial ten opens | 2481.80 / 2739.01 | 2313.78 / 2385.18 | not measured |
| Original history, 200 switches, final frame | 2293.64 / 2511.61 | 2027.86 / 2261.28 | not measured |
| Original history, prepared pair, final frame | 2212.88 / 2396.06 | 7.81 / 18.89 | not measured |
| Heavy history, initial ten opens | 8860.00 / 9213.91 | 9437.52 / 9991.47 | 2764.40 / 2861.69 |
| Heavy history, 200 switches, final frame | 8995.96 / 11032.10 | 9178.58 / 9959.22 | 2639.55 / 2838.57 |
| Heavy history, prepared pair, final frame | 9010.75 / 9623.47 | 7.63 / 49.76 | 8.31 / 16.28 |
| Heavy history, Exposure, twenty edits | 8354.15 / 9773.32 | 24.92 / 1425.87 | 30.97 / 2449.76 |
| Heavy history, AgX, twenty edits | 8340.75 / 8897.61 | 31.81 / 41.05 | 31.95 / 47.64 |
| Heavy history, lens scale, twenty edits | 6024.55 / 10354.13 | 8881.81 / 9531.56 | 34.03 / 2030.34 |
| Heavy history, profiled denoise, twenty edits | 9387.27 / 9650.58 | 9525.90 / 9890.33 | 36.70 / 57.70 |

The original bridge ignored the first manual lens edit while the native
`has_been_set` flag was false. Its two no-op lens samples were discarded.
The lens and denoise baseline rows instead use the same native manual-edit
initialization fix as the candidate, with the original two-buffer preview
and full synchronization. All twenty edits in each of those rows changed
visible pixels. This corrected control is not an untouched old release.

Prepared heavy-history navigation hits, including the prepared pair,
published a first frame in 0.50 / 2.73 ms (99 samples, maximum 3.17 ms).
Their final frames took 15.14 / 59.03 ms. The largest final-frame outlier
was 1858.69 ms. This meets the requested prepared-first-frame threshold,
but does not imply that every navigation request finds a prepared frame.
The 750 ms navigation dwell is shorter than a heavy CPU preload render.

For matched heavy-history misses, baseline and candidate medians were
9002.23 and 9442.72 ms: a 4.89% increase in the medians. The median paired
ratio was +5.85%, and the initial ten-open batch was +6.52%. The requested
no-more-than-5% cold regression is therefore not established on CPU.
Original-history initial opens improved by 6.77%.

Exposure and AgX CPU medians improved by more than 99%, exceeding the 25%
repeat-processing target for those modules in the alternating two-value
test. Those measurements revisit previously rendered values and can reuse
entire cached outputs; they do not measure dragging through unique values.
CPU denoise did not improve.
The first cache-building edit and occasional pipeline reconstruction still
produce long tails, as the Exposure p95 demonstrates. AMD OpenCL greatly
reduced heavy-workload processing, but its strict repeat-equality result
and tolerance-based quality result must be considered separately.

| Measured peak working set | Baseline | Touch |
| --- | ---: | ---: |
| Original-history CPU | 1408 MiB | 3320 MiB |
| Heavy-history CPU | 2461 MiB | 4078 MiB |
| Heavy-history AMD OpenCL | not measured | 1821 MiB |

The largest logged retained-cache count was 2038 MiB on CPU, below the
2048 MiB retained-cache budget. Native OpenCL reported a separate 2910 MiB
peak device allocation; shared allocations must not simply be added to the
process working set as if they were disjoint physical RAM.

The CPU control recheck and integration quality results follow in the
validation section.

A later twenty-edit lens-only control recheck measured 9157.88 / 9641.97
ms, versus the candidate's 8881.81 / 9531.56 ms. The earlier control began
near 5.6 seconds and ended near 10.8 seconds per edit; this later control
stayed near nine seconds. These serial measurements show substantial host
timing drift. They do not establish a CPU lens regression or a 25% lens
speedup. The recheck was stopped after all twenty lens edits; its partial
denoise samples are not used as a new twenty-edit result.

## Validation and limits

- CPU native tests passed exact cached/uncached preview comparisons,
  parked-pipeline reconstruction, cancellation, resizing, eviction,
  read-only preload parity, snapshot isolation, history operations,
  crop, export, recovery sidecars, and Unicode paths.
- All ten heavy-workload cold CPU RGB hashes matched the original
  baseline, and all eighty edited CPU RGB hashes matched their baseline
  or corrected lens control. Source files and copied sidecars were kept
  separate: all twenty original RAW/XMP SHA-256 values were unchanged.
- QML tests passed 50/50. Full offscreen Touch workflows passed at
  960x540 and 1280x720, and both editor screenshots were inspected.
- Strict GPU repeatability already differed by one channel level in one
  byte on the first render, when both comparison pipes still had the
  original two buffers. That failure is retained rather than counted as
  an exact-parity pass. Diagnostic GPU runs explicitly allow at most one
  channel level of repeat drift and record it in each JSON sample.
- The integration submodule was fetched at the checkout's pinned revision
  `3f2d1abaa2a473bf25a4f20c7f8db7d984e2746b`, with the exposure, profiled
  denoise, lensfun, and AgX-default cases and their original references.
  Its native CIEDE2000 helper uses maximum 2.3 and mean 2.3/3 thresholds.
- The integration runner completed four cases with one failure. Exposure,
  profiled denoise, and AgX-default passed CPU/reference and the separately
  checked GPU/reference and CPU/GPU comparisons. Lensfun failed: maximum
  dE was 6.02233 against the CPU reference, 2.99275 against the GPU
  reference, and 6.02233 between CPU/GPU. The failure was in headless
  `darktable-cli`, which does not use the Touch preview cache. Its cause
  has not been isolated; it is not treated as a passing integration test.
  A direct diagnostic invocation confirmed AMD OpenCL enabled and 11/11
  device events successful. The runner's optional ImageMagick comparison
  was unavailable, so all four CPU/GPU pairs and GPU/reference pairs were
  explicitly checked with its native CIEDE2000 helper as well.
- Comparing all ten heavy-workload CPU/OpenCL cold frames with that exact
  helper passed eight images and failed two: image 5 had maximum dE
  3.48733 and image 10 had 3.03399. Their means were 0.00490 and 0.04648.
  The maximum threshold applies even when the mean is small. AMD OpenCL
  therefore remains disabled in the user's existing Touch profile;
  GPU timings are diagnostic results, not a qualified default backend.
- CPU and GPU stress runs completed interrupted rendering, rapid and
  reverse navigation, a slider commit followed immediately by switching,
  reopen verification, missing-source rollback, and simulated low-memory
  pause/recovery. The committed value survived reopening. Native tests
  additionally covered history paste, crop, Undo/Redo, and export.
- Physical finger/controller input and actual display frame pacing have
  not been verified. Automated pointer/gesture tests do not establish
  those hardware results.

## Acceptance status and retained evidence

This remains a work in progress. Prepared-frame latency and repeat edits
to Exposure/AgX meet the requested targets. CPU denoise does not meet the
25% improvement target, the heavy cold-open 5% bound is not established,
AMD OpenCL does not pass the quality gate, and physical touch validation
remains outstanding. No database schema, XMP format, or module algorithms
were changed. The user's OpenCL preferences remain unchanged.

Local evidence is retained in the ignored `build/` directory:

- `performance-baseline-final.json`, `performance-cpu-final.json` and
  their logs: heavy workload, 200 switches, forty prepared-pair switches,
  and module edits; discard the original baseline's two no-op lens rows
- `performance-baseline-module-edits.json`: corrected two-buffer control
  for twenty visible lens edits and twenty visible denoise edits
- `performance-baseline-recheck.json`: later twenty-edit lens control
- `performance-baseline-original.json`, `performance-cpu-original.json`:
  original-history workloads
- `performance-gpu-diagnostic.json`: AMD workload, one-level repeat drift
  explicitly allowed; strict-run failure remains in `performance-gpu-final.log`
- `performance-quality-ciede2000.log`, `performance-integration-retry.log`,
  `performance-integration-pairs.log`, `performance-integration-gpu-proof.log`
- `performance-native-final-complete.log`, `performance-qml-final-complete.txt`,
  `performance-ui-960-complete.log`, `performance-ui-1280-complete.log`,
  and `performance-restored-build.log`

The temporary geometry-cache trial was discarded because it did not show
a benefit. Its partial benchmark is not included in the tables. The final
full Release build restored the measured `168ca1595` implementation.

## Continuous Exposure follow-up, October 28

The user reported that continuous Exposure dragging behaved like jumping
between values. A new probe sends changing values every 16 ms for two
seconds, rather than alternating only two already cached values. The
first full-input trial published no frames during that interval. Keeping
the export-style input for intermediate renders was insufficient when
the full pipeline cache was trimmed under memory pressure.

Interactive updates now use a separate native preview pipe with reduced
float mipmap input, while release uses the existing full-input final pipe.
The controller allows an interactive frame to finish, then sends the newest
pending value. Intermediate frames do not refresh the module schema, enter
the finished-frame cache, or change the final viewport resolution. All
retained pipeline buffers still count toward the same 2 GiB cache budget.
Low memory continues to pause neighboring preload and discard neighboring
frames before trimming the active pipelines to that budget.

With AgX, lens correction, and profiled denoise enabled, the final CPU probe
accepted 106 input changes and published 18 visibly different frames during
the two-second gesture. The first appeared after 332.42 ms; this is a
single diagnostic trace, not a median/p95 claim. Release preserved Exposure
0.75 and restored the 1280x853 final frame. One Undo returned to the value
before the entire gesture; Redo restored exactly the same final pixels.
A second gesture committed 0.9 and immediately switched images; reopening
confirmed that 0.9 was retained.

Native tests separately compared full-quality output after interactive
previews against a full-input-only reference, with exact pixel equality.
They also passed crop, export, history, reload, cancellation, eviction,
and sidecar recovery checks. QML tests passed 50/50. The pinned Exposure
integration case passed with maximum CIEDE2000 0.93148 and mean 0.00003.
Full offscreen workflows passed at 960x540 and 1280x720, including layout,
numeric input/reset, controller increments, history copy/paste, export,
and recovery. Both screenshots were inspected. The harness now waits for
popup transitions instead of assuming they finish within its 180 ms tick,
and allows 300 seconds for the complete workflow's full-quality renders.
Repeating the continuous-value probe with the available-memory policy
artificially capped at 1 GiB also passed: 111 input changes, 17 visibly
different intermediate frames, and 336.82 ms to the first frame. Final
resolution, Undo/Redo parity, and the immediate-switch saved value passed
again. This exercises the low-memory branch without limiting actual OS RAM;
its log is `build/slider-low-memory.log`.
These checks do not qualify AMD OpenCL or establish physical touchscreen
latency. The earlier twenty-pass navigation tables were not rerun for this
follow-up and retain their original implementation provenance.

Evidence: `build/slider-final-probe.log`, `build/slider-final-probe.json`,
its `.interactive.png` and `.final.png` images, `build/slider-native.log`,
`build/slider-qml.txt`, and `build/slider-integration-deltae.log`.
Full workflow logs are `build/slider-ui-960-final.log` and
`build/slider-ui-1280-final.log`; the final full build is recorded in
`build/slider-final-release-build.log`.

```sh
test-touch-performance --root '/path/to/photos' --rounds 0 --idle-ms 0 \
  --slider-probe --heavy --output build/slider-probe.json
```

## Reproduction

### Twenty-pass benchmark

Build with the checkout's MSYS2 UCRT64 Release configuration, then run with
the matching Qt plugins and native module/data directories available:

```sh
test-touch-performance --root '/path/to/photos' --rounds 20 \
  --idle-ms 750 --warm-pair --heavy --stress \
  --frames-dir build/frames-cpu --output build/results-cpu.json

test-touch-performance --root '/path/to/photos' --rounds 20 \
  --idle-ms 750 --warm-pair --heavy --stress --opencl \
  --diagnose-gpu-rounding --cache-dir build/kernel-cache \
  --frames-dir build/frames-gpu --output build/results-gpu.json
```

Omit `--heavy` for original-history measurements. Omit
`--diagnose-gpu-rounding` for strict repeat-image equality. Enable
`DARKTABLETOUCH_PERF=1` for cache hits and retained byte counts. The workload
selector requires two folders containing at least five CR3 files each.
All fixture mutations occur on temporary copies.
