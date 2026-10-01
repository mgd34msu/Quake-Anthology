# Q3 continuation inventory handoff

Status: partial implementation, frozen at root's handoff boundary. No source
acceptance or executable validation by this agent. The global source-only gate
remains closed. No further source edits after
frontend_resume delivered the root handoff notice.

Authored source packet: `/tmp/qa-q3-inventory-partial-20260930.sha256`.
The new C file is 345 lines, the new header 35, and changed Q3AS owner 507.
The exact three hashes were read from sha256sum; trailing-whitespace rg was
silent (exit 1). A second manifest check accompanies this handoff.

## Exclusive source work

- `src/app/frontend/q3_inventory.c/.h`: root granted sole new-file ownership.
  Draft has actual source-group collection/policy qualification, unique actual
  registry/presentation/media-library pointer rows and physical aliases; primitive
  QFQ3 envelope/metadata helpers; exact retained media resource pool/version rows;
  QMLB resource/image callbacks; Q3MS asset/cache and source-bus target callbacks;
  shared frame/root and real application collision lookup; exact immutable map
  entity span offset/length capture and pure candidate binding helper.
- `src/presentation/q3/assets_save.c`: root granted narrow private malloc text
  purity correction. New private_text preserves existing presence/count/bytes
  encoding while decoding owned malloc text directly. LOD paths, skin shader
  strings and hash-name entries no longer use qa_source_save_text; all read-side
  temporary name error paths free their allocation. No canonical session string
  interning is invoked for these real private producer fields. This correction
  has not received independent whole-file review.

## Incomplete bodies and order

The three declared codec entrypoints frontend_q3_checkpoint,
frontend_q3_prepare and frontend_q3_restore are NOT implemented. Their header
describes the intended boundary, not delivered behavior. Only inventory_destroy
has a public body. No frontend caller invokes the new helper.

Next implementation should:

1. Complete Q3AS refs using the actual content graph, shared scene namespace,
   parsed-holder inventory/stable tokens, real audio inventory and scoped root
   ownership callbacks. Use each unique registry's first actual source-group
   ordinal plus one for Q3 root destructor class; never pointer-cast IDs.
2. Capture QFQ3 complete metadata, unique QMLB prefixes/resources, one Q3AS per
   registry and one Q3PS/Q3MS per presentation; dispose component buffers and
   temporary collector on every failure while genuine outer frontend/content
   registry/root tokens remain held by parent.
3. Prepare must parse and qualify the entire QFQ3 envelope/trailing finish before
   importing any candidate QMLB prefix. Input bytes remain borrowed until helper
   destruction. Check every source media cache is genuinely empty before import;
   qualify physical cache count and each retained resource row afterward.
4. Late restore requires actual imported worlds/models/materials/frame/collision,
   map_resource and engine queues. Attach only genuine frame/map/entity aliases
   through prepare_restored, then Q3AS adopts true unique scene/preview owners,
   then Q3PS/Q3MS import actual continuation. Child import failure requires whole
   isolated candidate retirement; helper destruction frees only temporary rows.
5. Preserve registry-shared map binding aliases if an actual source topology ever
   exposes multiple presentations per registry: the current core prepare helper
   requires the registry still empty and cannot be called twice after first map
   binding without a qualified alias attachment path. Current real source factory
   produces one group/presentation/registry heap shared by its role leases.
6. Freeze complete actual helper, changed owner, needed API/caller dependencies
   for independent whole-source review before aggregate integration.

## Concrete remaining owner dependencies

True ownership qualification is not yet in Q3AS. world_decode serves both
borrowed mapworld and owned preview worlds, and the current model_fields can
accept owns_world=true for a supplied genuine mapworld resource/provider tuple.
A no-op adoption callback would still give Q3 double destructor authority. Root
requires explicit owned-vs-borrowed qualification before no-fail adoption.
Requested narrow header grant: add scene_owned_ready/world_owned_ready callbacks
to include/qa/q3_assets_save.h and require/call them before Q3AS adoption. Not
applied at the handoff boundary. Existing header hash:
`f04aa55bc1ab948ad0bae8e9e869c18f6fa73a5fde2f401a3fdb304019595539`.

frontend_model_inventory now owns NEW frontend/world_inventory.c/h. Its concrete
header is on disk. Pure lookup callbacks are frontend_world_encode/decode and
frontend_scene_root_encode/decode; scope checks are frontend_world_owner_ready
and frontend_scene_root_owner_ready(inventory,key,FRONTEND_SCENE_OWNER_Q3,owner,
error); no-fail transfers frontend_world_adopt/frontend_scene_root_adopt. Borrowed
map references survive genuine frontend adoption; scoped ready rejects foreign
destructor class/prior adoption. Actual collision remains the sole application
world geometry (physical key 1), never QWON/QMON construction. Header hash at
handoff: `bd7ebbd18239a8945d2cea5a3e76c9873d1b873214e126c0ab6a30e03e12547f`.

frontend_resume owns current draft APIs and source callers:

- qa_q3_presentation_binding_read observes actual options/frame/world/geometry/
  entity bytes under real registry/child observation tokens.
- qa_q3_presentation_prepare_restored binds genuine empty p/assets without
  ordinary load/world/frame/parser/service replay.
- frontend_source_group_q3_ready qualifies genuine source static callbacks and
  numeric constructor policy. Optional prepare_picture/video_frame/video_context
  NULL qualification is still an explicitly identified remaining fix.
- Actual restoring source_services still calls ordinary
  qa_q3_presentation_frame unconditionally. Parent was notified to skip that
  normal setter on restoring and let late pure binding preserve the saved frame.

These moving parent files are unreviewed, not part of this agent's source
acceptance. Handoff hashes:

- q3_presentation_save.h: ca88728ec0a531512e6d175b694e375e18c41a321f9a0ac2eeb10dd2413f784d
- q3/core.c: 8ba7f5c8f14ef8a05f1e952da6c00c09dd73338ddefb3583c9ea9f7533c14742
- frontend/source.c: de8238446d50182517c3efaa39a87659442420574ba3967e6b781869d9afc10b
- frontend/internal.h: db535672a090e95cf00eed175193eaf7ec9cea77c4ca415ef15f0b55da77eba6

material_owner_restore was asked for genuine readonly provider encode/decode
against QFMA's physical actual library roster, preserving the tuple
{mounts,images,materials,family}. No such API was yet present when inspected.
Do not reuse QFMA collect's empty-detached restoration checks after real library
import; readonly lookup must admit the imported actual owner. The helper has not
yet written calls to a guessed provider API. Existing material_inventory.h hash:
`67d34d4bf614b2859b791a981ef4464de3279b99bd7e384d5167c9c557f66256`.

## Source evidence read

Read actual Q3AS owner paths, Q3MS whole file, Q3PS retained/parser binding fields,
core lifecycle/map/frame paths, capture.c/.h, source factory/group observations,
scene_inventory.c/.h, scene_refs.c/.h, model_inventory.h, global scene_identity.h,
material_inventory.c/.h, content refs/graph, audio asset inventory and media
library full save/restore/resources/source admission. TypeScript donor actual
q3-client/assets.ts and cinematics.ts were read. Native media cache aliases retain
the first winning resource even with different saved request paths; helper must
restore those Q3MS aliases against exact retained asset identity rather than
reload paths. QMLB necessarily reconstructs immutable media headers; qualified
opaque Theora decoder reconstruction remains root-permitted bounded codec work.
Do not claim universal no-parsing/no-replay or completed runtime parity.

No QVM review was started: application_event_owner cancelled duplicate review
because world_source_link_owner owns the genuine QVM packet review.

