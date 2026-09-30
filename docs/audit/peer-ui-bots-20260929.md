# Q2 authored HUD, tokenizer and wheel source review

This independent pass reads the six frozen sources listed below. No confirmed
defect survived the inspected source traces. This is bounded source acceptance
of this packet, not acceptance of B32 or a running frontend.

The reviewer read all of `hud_q2.h`, `q2_layout.c`, `tokenizer.h`, `tokenizer.c`,
`hud_wheel.h` and `wheel.c`. Reference reads covered the complete donor
`src/ui/hud/q2-native.ts`, `q2-rerelease-layout.ts` and `wheel.ts`, the wheel and
carousel drawing block in `src/ui/hud/index.ts`, the general `Tokenizer` and its
whitespace predicate in `src/core/common-parse.ts`, the integer scanning kernel
in `src/core/game-numeric.ts`, and `src/app/bootstrap/network/q2-layout.ts`.
Donor paths are relative to `../quake-typescript`.

## Traced behavior

- All classic and rerelease layout dispatch branches were compared with the
  donor. Classic false `if` scanning stops at the first `endif`. Rerelease
  nested conditions consume operands while disabled, and unmatched conditions
  fail. The source's unconditional `story` branch is retained.
- Protocol-specific configstring bases and stat indices were compared with the
  donor. Classic protocol 4038 retains the classic layout. Foreign ammunition,
  selection and inventory use typed source readouts. Inventory scrolling,
  clocks, localization arguments and player substitutions were traced.
- General tokenization preserves unsigned ASCII whitespace, Unicode token text,
  comments, quotes without escape processing, quoted CRLF normalization,
  unterminated quote EOF, and the 1023 UTF-16-unit bound. Existing signed-byte
  COM parsing is not changed by this packet.
- Configstrings and binding strings are copied into frame scratch before
  further provider observations. Frame spans and source owners are borrowed
  for the whole draw by the public callback contract. Source callbacks must
  remain read-only during that borrow. This packet does not itself provide the
  outer application lease that enforces that contract.
- Wheel callbacks run while the private owner is busy; public mutation and
  destruction reject reentry. Item ordering copies scalar descriptors to
  retained storage; labels and images remain borrowed through the operation.
  Selection passes detached canonical item/source identity to the ordinary
  source selection callback after current ownership inspection.
- Carousel cycling, empty-ammunition filtering, source slot-zero deselection,
  fading, lock expiry, timeout selection and attack consumption match the
  inspected donor transitions. The command output is assigned only after a
  successful selection callback. The selection command retains holster even
  when it changes the carousel to closing or closed.

Two suspected issues were refuted in source. Nonfinite foreign ammunition is
rejected by `initialize` before integer conversion. Negative narrow-carousel
rectangles do not cause the supposed geometry rejection: the shared picture
geometry supports signed rectangle extents.

## Limits

No compiler, parser, build, test executable, game, benchmark or sanitizer was
run. Rendering, interactive input and runtime callback behavior remain
unverified. The application source HUD/wheel registration, actual source item
adapters, provider leases, ordinary command integration, frontend draw loop,
Q1 authored wheel-file decoding and the wider B32 scope remain outside this
review. No acceptance of those unimplemented or uninspected paths is implied.

Current hashes were read directly. `git diff --check` passed at this snapshot.

```text
93dea01d4895ca09f8e0e7a5054b75afe929969d4b435071df23b5b75f5a8ceb  include/qa/hud_q2.h
36912787770736bf921fbd63f1409918261b661cbbf24185461981f313f857fe  src/ui/q2_layout.c
6d7e78ab516cd47fc2b8713195230f3b47d2fcbe5473714c79fbf4a3a04288b8  include/qa/tokenizer.h
d3c560c031a6a74c52bdddc597628bf7917c9e4f919f4a05bb593ed639703c50  src/core/tokenizer.c
6c7e2b6f91966fc8e5cec1f688a6ae55d9c4ba04f97b0053c9060c1a69c4d01c  include/qa/hud_wheel.h
ad1c4c20231c194c5ba0fa2586087a2ea228392649d8cfbb7c0859c738264483  src/ui/wheel.c
```
