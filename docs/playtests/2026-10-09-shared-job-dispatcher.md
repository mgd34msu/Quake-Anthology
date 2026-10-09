# THE-2871 shared job dispatcher

CPU raster bands and MD5 model jobs now use `qa_jobs` in
`src/platform/jobs.c`. The renderer keeps its raster commands, indexed output
slots and ordered merge; its thread, semaphore, affinity and atomic work-claim
implementation is deleted. Nested dispatch runs inline. Creation uses the
calling process's affinity mask and physical-core topology.

GCC and Clang, including ASan/UBSan, preserve exact-once indexed output, nested
execution, rounding mode and the union of floating-point exceptions. Dispatch
creates no heap objects or threads. The production-linked renderer probe
compares queued and immediate RGBA, depth and stencil output, row retirement,
and four retail Q2 rerelease MD5 models at one, two, four and eight participants.
Those comparisons pass without warm-path heap calls. This is bounded renderer
and dispatcher evidence, not campaign or whole-frame allocation proof.

Packets: `/tmp/qa-the2871-jobs-20261009` and
`/tmp/qa-the2871-sdk-integration-20261009`. The isolated commit's source,
production/ASan build logs and existing core checks are recorded in
`/tmp/qa-the2871-isolated-build-20261009`.

The dispatcher cost measurement used 100 warm-ups and 600 samples, no debugger,
and affinity masks corresponding to one, two, four or eight physical cores.
For 64 indexed jobs, median / p99 microseconds were:

| Participants | Affinity | Median | p99 |
| --- | --- | ---: | ---: |
| 1 | `0,12` | 0.220 | 0.220 |
| 2 | `0-1,12-13` | 7.790 | 11.630 |
| 4 | `0-3,12-15` | 6.670 | 8.990 |
| 8 | `0-7,12-19` | 9.700 | 22.771 |

These measure dispatch cost, not a before/after frame speedup. Retail frame
performance remains a separate measurement.

The migrated call sites are `qa_render_workers_skin_batch`,
`raster_draw` and the queued `cpu_raster_flush` dispatch in
`src/render/cpu/raster.c`. That file no longer creates threads or semaphores,
queries affinity, or claims tasks through a second atomic counter. Its
runtime-growing vertex, projection, triangle and command arrays remain
THE-2874 allocation work; they are not claimed fixed here.
