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
- Aborting is correct only when the TypeScript throw reaches the top level. When
  TypeScript catches and continues, the recovery is behavior and must exist in C:
  dropping a peer, recording a diagnostic, settling a fallback, retiring an actor,
  cancelling a weapon or item. Use an error return where the TS function's
  callers are all in reach, or `qa_raise`/`qa_try` with `qa_cleanup_push` nodes
  (`include/qa/foundation/support.h`) where frames must unwind. That is the one
  mechanism: do not add another. A raise nobody catches prints and aborts, so
  converting an abort to `qa_raise` is safe. A
  `catch { cleanup(); throw }` that ends at the top level needs no cleanup in C.
  Input from the network, files, or guest code must never be able to abort the
  process where TypeScript would recover.
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
- Evaluation order: TypeScript evaluates call arguments, object and array
  literals, and operands left to right; C does not (GCC evaluates call
  arguments right to left, and the right side of `=` before the left). When
  two operands of one expression have effects that can see each other (reads
  from one cursor or random stream, a counter bump and a read of it), hoist
  them into locals in TypeScript order: `qa_vec3 v; v.x = read(r);
  v.y = read(r); v.z = read(r);`. `tools/eval_order_audit.py` finds them.
- Numbers printed where TypeScript uses `String(n)` or a template literal use
  `qa_net_js_number` (or `QA_JS_NUMBER(n)` with `%s`), never `%g`/`%.17g`:
  `%g` loses digits and spells `0.00001` as `1e-05`.
- JS number semantics: where the TypeScript runs a JS built-in or operator,
  the C must return what Bun (JavaScriptCore) returns, NaN, -0, infinities and
  out-of-range values included. Use `include/qa/foundation/js.h`, never the C
  library look-alike: `qa_js_min`/`qa_js_max` for Math.min/max (fmin/fmax drop
  NaN); `qa_js_round` for Math.round (round/lround tie away from zero,
  floor(x + 0.5) fails at 0.49999999999999994); `qa_js_pow` for Math.pow and
  `**` (JSC squares integer exponents 0..1000, so glibc pow differs in the
  last bit); `qa_js_hypot2/3` for Math.hypot (three arguments are not
  sqrt(x*x + y*y + z*z) or nested hypot); `qa_js_to_int32`/`to_uint32`/
  `to_uint8`... for `| 0`, `>>> 0`, shifts, masks and typed-array stores (a C
  cast of NaN or an out-of-range double is undefined); `qa_js_bit_and/or/xor/
  not`, `qa_js_shl/shr/ushr` (or int32 operands) for JS bitwise operators on
  numbers, never int64_t arithmetic, which keeps bits above 2^31 and never
  goes negative; `qa_js_parse_int`/`parse_float`/`string_to_number` for
  parseInt/parseFloat/Number(string) (strtol/strtod accept "inf", hex floats
  and signs before 0x); `qa_js_to_fixed` for toFixed (printf rounds ties to
  even and prints "-0.00"); `qa_js_sort`/`qa_js_stable_sort` for
  Array.prototype.sort (qsort is unstable). A floating condition treats NaN
  as true in C and false in JS. `tools/js_semantics_audit.py` finds the
  sites; classify each one it flags in `tools/js_semantics_audit.tsv`.
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

## Tests

- `CHECK_ABORTS` fork children (the `toThrow(RangeError)` reproduction)
  must call `prctl(PR_SET_DUMPABLE, 0)` first: expected aborts otherwise
  flood systemd-coredump and desktop crash notifications. `make test`
  also runs under `ulimit -c 0`.

Per-file status is generated into `docs/manifest.tsv` by
`tools/gen_manifest.sh`; regenerate after each module lands.
