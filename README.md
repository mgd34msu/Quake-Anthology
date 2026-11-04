# Quake Anthology (C)

Faithful C port of `quake-anthology-ts` (TypeScript/Bun): one shared engine
that runs Quake, Quake II and Quake III content together. The engine is C17.
The only TypeScript here is bun oracle scripts under `tests/**/oracles/` and
`tools/`, which run the TypeScript project to produce expected test values.

> **Note on this repository's history.** The previous project that lived on
> `main` is preserved unchanged on the `archive` branch. This `main` branch
> holds a new, much more recent port of the TypeScript project. It is still
> in progress, with a lot of work left to do (see Status below).

The port mirrors the TypeScript tree file for file:

| TypeScript | C |
|---|---|
| `src/<module>/<name>.ts` | `src/<module>/<name>.c` + `include/qa/<module>/<name>.h` |
| `src/<module>/index.ts` (re-exports) | `include/qa/<module>.h` umbrella header |
| `tests/<module>/*.test.ts` | `tests/<module>/test_*.c` |

File basenames convert hyphens to underscores (`bounded-queue.ts` becomes
`bounded_queue.c`). Symbols use the `qa_` prefix. See `docs/PORTING.md` for
the full conventions and `docs/manifest.tsv` for per-file port status.

## Build & test

Requirements: a C17 compiler, GNU Make, and the development packages for
SDL2, FreeType, Vorbis, Theora, OpenGL, libcurl, OpenSSL (libcrypto) and
zlib. The TypeScript build reaches the same libraries through Bun FFI.
Comparison tests also need `bun` and a checkout of `quake-anthology-ts` next
to this repository.

```sh
make        # build/quake-anthology
make test   # every test binary, then --version/--help smoke checks
```

`make test` stops at the first failing binary. Tests that need retail game
data read it from `QTS_TEST_CORPUS` (default `~/qfiles`) and skip without it.

## Game data

Retail assets are never committed. Point the engine at an existing install,
for example `~/qfiles` (`Quake/`, `Quake 2/`, `Quake 3 Arena/`), with
`--content-root`.

## Status

**Work in progress.** Most of the TypeScript tree is ported, and the core
paths are checked line for line against the TypeScript project:

- The shared simulation matches TypeScript state and checkpoint output in
  all ten comparison cases (Quake, QuakeC, Quake II classic/rerelease/native,
  Quake III and QVM, mixed recipes and presets).
- Client/server networking matches the TypeScript client for NetQuake,
  QuakeWorld, Quake II, Quake III and the unified cross-game protocol,
  including clients joining a server from another game profile.
- Dedicated servers on Q1 e1m1, Q2 base1 and Q3 q3dm1 match TypeScript
  output.

A lot of work remains:

- A full function-by-function review against the current TypeScript code is
  only partly done (world and movement are complete; most content, compat,
  bots, network, UI and app code are not).
- Some TypeScript tests are not ported yet, and a few known test failures
  and memory leaks are open.
- The startup graphics layer is incomplete, and `main` does not yet run
  through the full Application path.
- Optimization has not started.

`docs/PORT_COMPLETION.md` tracks the remaining work and open items in
detail; `docs/manifest.tsv` and `docs/port_review.tsv` track per-file status.

## License

GPL-2.0-or-later. See `LICENSE`.
