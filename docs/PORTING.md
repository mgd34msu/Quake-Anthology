# Porting quake-anthology-ts to C

Ground rule: only behavior verified in TypeScript *code* (and its tests) is
treated as fact. Markdown in either repository states intent and may be
stale; when a doc and the code disagree, the code wins.

## Source of truth

- Source: `~/Projects/quake-anthology-ts/src` (2,104 `.ts` files).
- Oracle: `~/Projects/quake-anthology-ts/tests`, run with `bun test`.
  Every ported module keeps its ported tests green against the same cases.
- Assets for manual/runtime checks: `~/qfiles` (never committed).

## Layout

- `src/<module>/<name>.ts` -> `src/<module>/<name>.c` and
  `include/qa/<module>/<name>.h`. Hyphens become underscores.
- `index.ts` re-export barrels -> `include/qa/<module>.h` umbrella headers.
- The TypeScript foundation comments name flat `include/qa/*.h` headers;
  the C tree nests by module because many modules share basenames
  (`index.ts`, `types.ts`). This is a naming accommodation only.

## TypeScript -> C conventions

- Classes become a struct plus `qa_<name>_*` functions. `init`/`destroy`
  replace constructors/GC; ownership is documented per header.
- Generics (`BoundedQueue<T>`, `EventRing<T>`, `FixedPool<T>`) become
  element-size storage copied with `memcpy`. Pointer payloads are stored
  by storing the pointer value itself.
- `throw new RangeError(msg)` on invalid arguments becomes
  `qa_range_error(msg)`, which prints the same message and aborts. The
  TypeScript throw propagates to the top-level crash handler, so failing
  loudly preserves observable behavior. Recoverable "full" conditions keep
  their exact sentinel returns (`-1`, `0`, `false`).
- Allocation failure has no TypeScript counterpart (GC never reports it);
  constructors return `bool` for OOM only. Invalid arguments still abort.
- `number` integers checked with `Number.isSafeInteger` become `int64_t`
  with the same `2^53 - 1` bounds where the bound is observable
  (saturating counters, ring id exhaustion, packed keys).
- Typed-array semantics are preserved where the TypeScript relies on them:
  out-of-range loads that throw stay fatal; out-of-range typed-array
  stores that JavaScript silently drops are dropped in C too. Each such
  site is commented.
- `Math.fround` staged rounding is reproduced by computing in `double`
  and casting to `float` at exactly the `Math.fround` points. The build
  uses `-ffp-contract=off` and never `-ffast-math`, so binary32 results
  match the TypeScript bit for bit (verified by ported tests).
- Closures become a function pointer plus `void *ctx`.
- `async`/promises and Bun FFI have no C counterpart: platform libraries
  (SDL2, OpenGL, FreeType, Vorbis, Theora) are linked directly, and
  asynchronous flows become synchronous or explicitly pumped state
  machines. Each such change is noted at the call site.
- Strings: TypeScript strings are UTF-16; C strings are NUL-terminated
  bytes. Length-sensitive logic notes where UTF-16 unit counts and byte
  counts can differ (non-ASCII input).

## Order of work

Dependency order, each step with ported tests before moving on:

1. `foundation` (done) -> `core` -> `contracts` (types only; const data).
2. `formats`, `content` (archives, catalog) for `--list-content`.
3. `console`, `settings`, `persistence`, `text`.
4. `platform` (SDL/GL/FreeType/Vorbis/Theora direct linkage).
5. `world`, `movement`, `camera`, `materials`, `render`, `audio`, `media`.
6. `network`, `guest`, `compat`, `bots`, `input`, `ui`, `llm`, `capture`,
   `debug`.
7. `app/bootstrap` and `main` last: they wire everything together.

Per-file status is generated into `docs/manifest.tsv` by
`tools/gen_manifest.sh`; regenerate after each module lands.
