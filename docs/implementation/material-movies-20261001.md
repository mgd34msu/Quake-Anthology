# Material shader movie ownership

Author runtime `01a0fa23-a983-7e62-b790-f0b734aaffcd`, inherited model `gpt-6.1-sol`, effort `xhigh`. JEV plan 7, contribution `w_326b57265ce84442956b8bcc3233f28e`, parent Root B34 `w_2959dd54d5294d8a82e5d1fec94e2e5b`.

The owned movie bodies are implemented, and actual frontend constructor, renderer, frame, cleanup and cold callers are being integrated. Work continues directly through those required callers. No configure, compiler, build, tests, project scripts, parser execution, runtime, performance work or Git writes were performed by this author.

## Required behavior and source evidence

`quake-typescript/src/app/bootstrap/assets.ts` caches material movies by actual provider and normalized path. Names without slash or backslash gain `video/`; the selected mounts open the path before format validation. RoQ, CIN and OGV movies loop silently. Repeated failed paths retain the original error instead of opening again. The actual application callers omit a media-clock override, so the donor clock is monotonic wall time, separate from transformed Source command time.

`quake-typescript/src/media/material.ts` advances playback in `MaterialCinematic.resolve`, at each reached shader stage. Its no-picture publication is transparent and retains the genuine asset dimensions. `quake-typescript/src/media/roq-playback.ts` rebases a shader movie's epoch across a gap exceeding 100 ms. Advancing every invisible movie at global frame start would change that behavior. SDK `tr_shader.c`, `tr_shade.c` and `cl_cin.c` corroborate directive-time start and reached-stage run/upload; the TypeScript application remains the behavioral reference.

The native generic material movie helper originally had no application producer. Actual native/external renderer options also omitted `video_frame`. Their genuine parent fields and constructors remain Frontend-owned. Material receipt/parser/order policy and compositor remain NativeView-owned. This author owns the new frontend movie child and the exact lower media loans granted by Root.

## Actual owner graph

Each real material provider supplies its retained frontend, VFS, image bank, material library and shared media cache, plus a pure function proving that exact constructor/live/candidate tuple. The library retains `frontend_material_movies_start` and that real owner. A raw callback reader proves the exact function before casting its context to the typed owner.

Successful path rows own their retained path, asset reference, immutable initial image reference and private initial-publication frame. The lower registry owns their actual cinematic instances. Initial shader publication stays transparent at the asset dimensions even when CIN or OGV has already decoded its first picture. Its publication revision remains `UINT64_MAX` until a reached stage uploads decoded pixels, including decoder revision zero; cold import preserves that distinction. Failed rows own their normalized path and original native error, and have no playback, asset, image or target. Public row enumeration identifies that state explicitly.

The media clock samples the actual frontend's `wall_time_ns / 1e6`. Owner creation and frame admission bind `&frontend->frame` without ticking, including real renderer draws during UI/CG initialization. The reached renderer callback binds that same actual frame and sequence, ticks that instance and emits its immutable image barrier into the submitted frame. Repeated reached callbacks remain repeated ticks.

## Prepared replacement

The child prepares after the actual destination image bank and material library exist, before replacement skin registrations. Existing paths retain their actual playback and initial handles. New paths load from the real provider into an offside media cache and registry, with a private publication frame; preparation does not append to the submitted frame or change installed cache rows.

Ready seals the movie child before material/order/bank sealing. Final readiness reads the genuine source association, exact prepared callback and actual bank-ready proofs. Publication splices retained asset nodes, adopts prepared registry storage and publishes the row table without allocation or callbacks. Disposal retires order and material destinations, then the movie child, then the bank. A refused abort or finish retains the actual ticket and ownership for retry.

## Cold continuation

QFMM preserves path-cache physical extent/order, target allocator, wall anchor, failure errors, real asset and initial image references, private initial command/image capacities, each complete cinematic checkpoint and retained publication/frame binding. QMMR preserves actual registry physical extent/order, enabled state and real submitted-frame binding. Restore uses imported provider assets/images/material records, qualified decoder reconstruction and retained image/frame references. It never opens a provider path, registers a material, adds a movie, ticks playback or invokes Source initialization.

Every imported material video receipt, including a video directive later overridden within a stage, must correspond to the matching successful row's exact initial image. Failed registration receipts are removed by the parser/library's genuine failure cleanup; no NULL receipt is treated as a successful authored movie.

AuditRendering found that image import assigns fresh identities in newest-first saved order. The old numeric sort therefore cannot govern restored lookup. Registry rows retain physical registration order; exact initial-image lookup scans those owned rows. Prepared publication concatenates actual source and candidate rows. The frontend also qualifies registry physical order against successful path rows, so recapture retains the same real row reference keys after identity rebasing.

## Source coverage

The author read the lower cinematic/media/cache/registry/codec owners, CIN/RoQ/OGV playback, cinematic publication and frame codec, campaign and numeric Q3 movie callers/codecs, actual provider and material inventory seams, relevant donor asset/playback/material registration and SDK run/upload bodies. Frontend owns the actual parent constructor/frame/save/context changes; NativeView owns the material parser/order/compositor changes. Root coordinates execution checks and integration as those implementations land.
