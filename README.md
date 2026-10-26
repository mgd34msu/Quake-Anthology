# Quake Anthology (C)

Faithful C port of `quake-anthology-ts` (TypeScript/Bun). No TypeScript lives
in this repository: implementation is C17, plus small shell scripts under
`tools/`.

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

Requirements: a C17 compiler and GNU Make. SDL2, FreeType, Vorbis and zlib
are linked directly once the platform modules that need them are ported
(the TypeScript build reaches the same libraries through Bun FFI).

```sh
make        # build/quake-anthology
make test   # foundation unit tests + --version/--help smoke checks
```

## Game data

Retail assets are never committed. Point the engine at an existing install,
for example `~/qfiles` (`Quake/`, `Quake 2/`, `Quake 3 Arena/`), with
`--content-root`.

## Status

Port in progress, module by module in dependency order. `src/foundation`
is ported with tests; the remaining modules are tracked in
`docs/manifest.tsv`. Behavior that is not yet ported fails explicitly
instead of silently diverging.

## License

GPL-2.0-or-later. See `LICENSE`.
