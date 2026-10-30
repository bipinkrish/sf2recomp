# Performance measurement: CPU buffer copies

On the Ryzen 7 7735HS (8 cores / 16 logical CPUs), a paired Story-dialogue test
improved from **49.38 to 55.87 FPS**, about **13.1%**. This measures the existing
software Vulkan path at 1280×720. It does not establish combat performance,
Windows performance, hardware GPU performance, or complete game compatibility.

| Steady-state measurement | Before | After |
|---|---:|---:|
| Average FPS | 49.38 | 55.87 |
| Draws per frame | 120 | 120 |
| Skipped draws | 0 | 0 |
| Render passes per frame | About 7.7 | 1 |
| Submissions per frame | About 8.7 | 2 |
| CPU-copy GPU wait per frame | About 13.3 ms | Removed under guarded conditions |
| Process CPU use | About 9.4 logical CPU equivalents | About 10.4 |

Two sequential 30-second runs used X11, lavapipe/llvmpipe LLVM 20.1.2, fresh save
directories, copies of the same warm shader cache, default settings, profiling,
and A-button presses at frames 180/400. Six two-second samples per run beginning
at 18 seconds were averaged. This is one paired workload measurement, not a
multi-scene benchmark or confidence interval. More completed frames also increased
CPU utilization. The captured frame 450 shows the same Sensei dialogue in both
runs; both were inspected directly. Small animation differences remain.

The local verification artifacts are ignored build outputs:

- `build/review/perf-baseline/lasrecomp.log`
- `build/review/perf-copy/lasrecomp.log`
- Each directory also contains `thread-samples.json` and
  `build/frames/frame_00450.png`.

## Change and synchronization

Every NVN buffer-to-buffer copy previously flushed and waited for all pending
GPU work before performing a host `memmove`. These waits also split compatible
render passes and submissions.

The new path skips that wait only when **no whole heap is imported, no lazy heap
chunks are used, and GPU-overlap mode is enabled**. In that configuration the GPU
reads staging snapshots rather than the CPU's guest memory. Later draws get
fresh snapshots because the copy invalidates the guest-copy cache; already
recorded draws retain their previous snapshots. Imported heaps still wait because
the GPU may access either range directly. Overlap-off still waits because GPU
timestamp resolution can write guest memory. Texture readbacks retain their
existing synchronous flush.

`tests/test_cpu_copy.py` compiles the actual copy helper and staging-cache code.
It checks preserved old snapshots, refreshed later snapshots, overlapping
`memmove`, and all three conservative synchronization guards.

An unrelated Zelda-specific widened-lighting instruction patch was also removed.
It did not match this workload's shaders. Shader cache keys include generated
source, so changed shader output produces a different SPIR-V cache key.

## Repeat the workload

Generate a warm cache by running the game once. Before each measured run, use a
new save directory and seed it with the same cache. Run the following once with
the original copy implementation and once with the guarded implementation:

```sh
bench_dir="$PWD/build/review/perf-repeat"
mkdir -p "$bench_dir/cache"
cp -a build/run/cache/. "$bench_dir/cache/"
LARECOMP_DATA_HOME="$bench_dir" LARECOMP_DATA_ROOT="$bench_dir" \
  LARECOMP_INPUT=180:A:3,400:A:3 LARECOMP_NO_USER_INPUT=1 \
  LARECOMP_VK_PROF=1 LARECOMP_SHOT_AT=450 \
  timeout -k 5s 30s tools/run_linux.sh --software
```

Use separate directories for the two runs. Exclude startup and screenshot stalls
from steady-state comparisons, and inspect the screen to confirm the same scene.
The measurements above used the cache from `build/review/final-portable` in both
runs. That local folder is not a required source-distribution input.

## CPU cores and native GPU follow-up

The runtime already uses host threads for guest threads, a render queue, Vulkan
command recording, a dedicated shader translator and parallel shader/pipeline
compilation. On this machine the compiler pool can use 12 workers; idle workers
are expected once shaders are ready. Lavapipe adds its own CPU rendering workers,
which accounted for most measured CPU time. Making every core busy would not
necessarily improve throughput or preserve the game's ordering.

AMD hardware rendering was not retested because guest-heap imports previously
triggered kernel HMM faults. The current software launcher sets `NO_GMEM=1`:
arbitrary shader global-heap reads then return zero. That is an incomplete fallback,
not a substitute for correct native GPU memory support.

A safe hardware path needs ordinary Vulkan-managed buffer backing, dirty-range
uploads, GPU-write readbacks at the proper fences, and correct global-pointer
addressing. Existing copy/chunk helpers can support that work, but arbitrary
shader accesses and CPU/GPU write ordering must be covered before claiming
hardware compatibility or performance gains.
