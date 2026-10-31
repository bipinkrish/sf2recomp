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

At the time of the CPU-copy measurements, AMD hardware rendering was not retested because guest-heap imports previously
triggered kernel HMM faults. The current software launcher sets `NO_GMEM=1`:
arbitrary shader global-heap reads then return zero. That is an incomplete fallback,
not a substitute for correct native GPU memory support.

A safe hardware path needs ordinary Vulkan-managed buffer backing, dirty-range
uploads, GPU-write readbacks at the proper fences, and correct global-pointer
addressing. Existing copy/chunk helpers can support that work, but arbitrary
shader accesses and CPU/GPU write ordering must be covered before claiming
hardware compatibility or performance gains.

The October 31 hardware follow-up instead uses a shared, coherent driver-owned
heap mapped at the guest address. CPU and GPU access the same backing memory;
there is no separate mirror to upload or read back. RADV now disables the host
import extension and userptr/probe paths in this mode, disables write tracking
before replacing the anonymous mapping, binds the buffer before publishing the
mapping, and removes the CPU mapping before freeing driver memory on reset.
Allocation failure stops boot. An isolated compute test checked both directions
of visibility at three widely separated heap offsets through two allocation
cycles. A Radeon 680M game run reached frame 1,200 in the Sensei movement tutorial
with strict Khronos validation and no reported errors. Warm sections logged
59.8 FPS at 1280x720/60 FPS; cold shader compilation and loading caused dips.
These are scene-specific observations, not a matched GPU speedup benchmark or
full combat validation. Logs are under `build/validation/gpu-amd-validation-fixed`
and `build/validation/gpu-heap-roundtrip.log`.

A further 3,000-frame scripted run using the current 120 FPS repository settings
reached a Kenji sparring fight, with movement/attack inputs and visually checked
captures at frames 2,400 and 3,000. Strict validation reported no errors, and the
process exited normally. Warm sections logged about 119.2 FPS; loading and shader
compilation still caused dips. Evidence is under
`build/validation/gpu-amd-tutorial` (the directory name reflects the planned
test, which progressed into sparring). This is an initial combat-scene check,
not a complete fight/progression/stability qualification.

After repairing the host driver and rebooting into 7.0.0-38, the RTX 4070 Laptop
GPU (NVIDIA 595.99.02) also passed a 3,000-frame strict-validation run reaching
Kenji sparring. `nvidia-smi` confirmed `sf2` running on the dedicated GPU. Its
X11 swapchain selected FIFO and steady sections logged 59.9 FPS at 1280x720,
with a configured target of 120. This presentation behavior differs from the
AMD mailbox test and does not establish a relative GPU speedup. Cold shader
compilation and scene loading caused dips. The separate driver-owned heap
compute round-trip also passed on NVIDIA across two allocation cycles.
Evidence: `build/validation/gpu-nvidia-post-reboot`.
