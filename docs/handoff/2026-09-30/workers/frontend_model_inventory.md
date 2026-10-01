# Frontend model inventory, native Q3 END, and unfinished world inventory

This is the exact lane state when root requested the new-session handoff. The world inventory stopped at its header boundary. Its implementation has not been written.

## Completed immutable model inventory

Paths and currently verified hashes:

- `src/app/frontend/model_inventory.c`: `484005849f2d61dfb8205f0751ad5a16a447b1e5e74f42e5b34f1c5247cdbe83`
- `src/app/frontend/model_inventory.h`: `9bb7983f996e913a18d992af5973c7f16b90f6c938a362838d92b4ede2b7d3b6`
- Accepted manifest: `/tmp/qa-frontend-model-token-source-20260930.sha256`.
- Source evidence: `/tmp/frontend-model-inventory-source-20260930.md`.

The complete QFMI typed codec preserves real parsed model/animation holders, physical pointer aliases, distinct equal-byte holders, resource-version/digest/VFS provenance, and original source-relative views. Restore copies arrays and source buffers without replaying load, registration, scaling, or animation. Pure QMON resolvers qualify the actual holder and source bytes.

Independent model_owner_restore source acceptance includes the genuine MD5 frame allocation and full in-range mesh parent metadata. Animation hierarchy still requires prior-joint ordering.

Actual per-holder tokens retain both immutable holder use and inventory metadata. Restore initially holds staging references. Actual cache/QMON/Q3 borrowers retain holders before frontend_models_install releases staging. Last use frees parsed arrays and retained resource versions. Retired physical holes remain in the old inventory. Tokens survive frontend_models_destroy and expose genuine current provenance through frontend_model_lease_source/frontend_animation_lease_source. Installed inventory never reuses a discarded construction content graph. Future save uses fresh live producers and the current graph.

These two paths are released. Their aggregate caller/lifetime closure remains owned by frontend_resume and Q3/QMON consumer owners.

## Completed native Q3 ClientEndFrame wrapper

Paths and currently verified hashes:

- `src/app/application/native_q3_end_frame.c`: `4b97a5159862b9cc0381944499325a75266a2c694ee6e181f3d4a017a7915b83`
- `src/app/application/native_q3_end_frame.h`: `bcb818d759be35a714d93980871fa37d24cf051efcf71c44873a9b177049b0fe`
- Current accepted manifest: `/tmp/qa-native-q3-end-frame-water-20260930.sha256`.
- Earlier `/tmp/qa-native-q3-end-frame-20260930.sha256` is superseded by the water correction.
- Source evidence: `/tmp/qa-native-q3-end-frame-20260930-source.md`.

model_owner_restore accepted both whole corrected files after two hash checks and whitespace inspection. The wrapper requires the real ADVANCING application, Q3 END phase, provider/frame fields, native time retained at ENTRY, and full actor generation in its actual in_use physical source client row. It holds the native console owner through the operation and stops further work after legitimate actor/provider retirement.

Spectator selection uses real sess7/follow1/follow2/CONNECTED records. It observes target state once, invokes the actual source FOLLOW copy, preserves explicit FREE/ClientBegin fallback and scoreboard behavior, and never implements read-time target aliasing. Native FOLLOW follows the TypeScript preserve-authority copyFrom contract: copied source fields remain real source fields while independently owned follower body/inventory words remain canonical. External SDK whole-PS behavior is separate.

Regular END uses qa_q3_client_movement_water_read. This is the retained native GAME gentity water authored before ClientEvents/teleport; raw selected control water was an independently confirmed defect and has been removed. It calls actual end_prepare, then genuine cached g_smoothClients BG publication at PS commandTime and the actual pending-event producer once.

The last source read found both qa_q3_client_end_prepare in `src/gameplay/q3/feedback.c` and qa_q3_client_world_effects in `src/gameplay/q3/player.c` now defined. Their source acceptance and broader core/provider/Think integration are separate from wrapper2 acceptance. Do not describe those definitions as absent; do not infer complete pipeline acceptance from the wrapper packet.

Shared source fixes previously communicated to their actual writer include EF_VOTED|EF_TEAMVOTED mask 0x84000, genuine qa_q3_wire_policy/movement_dir type names, copied commandTime ownership, and post-copy PS field assignments. Source/wire owners reported the corrections and preserve-authority backing changes. Those shared files are not part of this lane's wrapper freeze.

## World inventory: actual written state

- Sole written path: `src/app/frontend/world_inventory.h`.
- Verified hash: `bd7ebbd18239a8945d2cea5a3e76c9873d1b873214e126c0ab6a30e03e12547f`.
- `src/app/frontend/world_inventory.c` does not exist.
- No world inventory packet has been frozen, reviewed, or accepted.
- All functions declared in world_inventory.h remain unimplemented. Dependent q3_inventory code is being written against that header, so the missing implementation is an explicit prerequisite.

The header defines:

- frontend_world_inventory_capture/checkpoint/restore/destroy.
- Actual world/model physical extent and read views.
- frontend_world_encode/decode and frontend_scene_root_encode/decode, with nonzero physical root ordinal plus one.
- FRONTEND_SCENE_OWNER_FRONTEND/Q3/VISUAL and actual destructor metadata.
- Scope-qualified frontend_world_owner_ready/frontend_scene_root_owner_ready.
- No-fail frontend_world_adopt/frontend_scene_root_adopt and final frontend_world_inventory_ready.
- A model root view with true immutable source provenance, destructor scope, and actual visual cache path.

Q3 scope owner is the actual registry's first source-group ordinal plus one. Visual scope owner is its actual appearance-owner ordinal plus one. Frontend map owner has owner/row zero. Row metadata should retain actual cache/model-table ordinal plus one. Do not replace these with pointer casts or synthetic traversal counters unrelated to the real producer.

## Interface agreement and real existing producers

frontend_resume owns detached frontend constructors and aggregate assembly. It agreed that this helper should qualify genuine paired ROOT/SOURCE/VISUAL image/material owners itself through existing root fields, frontend_source_group_read and frontend_visual_owner_read. The helper must not create another image/material registry or allocate substitute owners.

Existing dependencies read:

- `scene_inventory.h/.c`: actual scene_inventory capture; fresh parsed-holder inventory; actual world_source {world,resource,files,images,materials}; real deduplicated QMON root traversal; scene_inventory_namespace uses root ordinal plus one per kind.
- `scene_refs.h/.c`: frontend_scene_model_scope {identity {space,owner},models}; frontend_scene_model_refs pure content/image/geometry/material/numeric callbacks and stable content token retains; token provenance adapters.
- `scene_identity.h/.c`: one shared image/geometry/material/frame and numeric producer namespace; restores allocations/identities before callbacks; actual qualify_world/qualify_model and stable bind_frame APIs.
- `capture.h/.c`: real frontend capture opens unique Q3 registries before descendant root tokens and tears them down in reverse order. Root collection includes visual caches, main world, Q3 services worlds, and actual Q3 model-holder descendants.
- `visual_restore.h` and visuals.c: actual owner/cache row getters and restored-cache attach. Scene ownership transfers only on successful attach, with genuine immutable holder/resource retention and saved physical cache ordering.
- `qa/q3_assets_save.h`: actual model/LOD/scenes/owned-world/source getters and true Q3AS codec with full-stream adoption.
- `qa/scene_world_save.h`, `render/scene/world/owner_save.c`: actual QWON detached owner codec and source/image/material/geometry qualification.
- `qa/scene_model_save.h`, `render/scene/models/owner_save.c`: actual QMON complete replacement-tree owner codec, retained content-token acquisition and teardown.
- `audio_restore.h`, audio_inventory.h and parent audio helper: actual bank/asset inventory is separate and must be reused by the Q3 helper. World inventory does not invent sound or media owners.

Parent map attachment must resolve the saved world ordinal and actual qa_application_map_read/content resource/name. It must not invoke scene_sync or build the BSP world again. Its map resource requires its own actual retained reference after adoption.

## Missing actual world policy producer

No qa_scene_world_options_read declaration or definition exists in the last inspected `include/qa/scene_world_save.h` and `src/render/scene/world/save.c`.

Requested genuine producer:

    bool qa_scene_world_options_read(const qa_scene_world *, qa_scene_world_options *);

It should read the world's actual retained options only when qa_scene_world_observation_ready permits observation. All pointer spans/string values borrow the real world/capture lifetime. This shared-path change was requested from root, but not granted or implemented when root stopped this lane. Do not edit those shared paths without assigning their single writer.

This is needed to capture actual constructor policy before QWON codecs, keeping source_qualify callbacks pure. Do not guess policy from frontend factory defaults or synthesize a whole-default inventory. Do not allocate/capture policy secretly inside source_qualify. The planned world record retains actual scalar image/world policy, exact nullable sky text, and length/digest qualification of real external lighting, palette, and translation bytes from this producer. QWON itself preserves the private owned byte buffers.

## Planned implementation, not yet written

Capture should consume actual scene_inventory roots in its retained physical order. Each root gets its actual paired image/material owner kind and physical index, content view/pool/resource-version provenance, genuine policy, and exactly one full QWON/QMON blob. Main world provenance comes from f->map_resource/f->mounts; Q3 preview world provenance comes from its actual owned model holder. Parsed QMON sources resolve through the fresh immutable model inventory. Do not merge distinct holders or scene owners because source bytes match.

Identify actual destructor authority by reading f->scene_world, every visual owner/cache row, and every unique Q3 registry's real model table. The same Q3 row may alias LOD scene pointers. Deduplicate those aliases within that genuine row. Reject repeated destructor authority across distinct cache rows or registries. Reject any captured anonymous root without a real producer.

Proposed capture metadata distinguishes paired heap owner scope from scene destructor scope. A scene may borrow an image/material owner while another actual root owns its destruction. Q3 borrowed mapworld references must remain resolvable after the frontend adopts its destructor.

Restore order must use the actual prepared detached heaps, complete immutable parsed inventory, restored resource state/material libraries/order, full shared namespace and stable frame address. Parse the complete outer metadata/blob stream before root imports. Construct each root once with qa_scene_world_owner_restore/qa_scene_model_owner_restore, then qualify the actual complete root in the shared namespace. Qualified immutable BSP parsing is permitted inside the real owner import; live VFS acquisition/build/registration replay is not the restore design.

The inventory owns unadopted candidate roots. Owned consumers call scoped owner_ready before their complete codec/admission succeeds, then the no-fail adopt operation transfers constructor ownership. Final ready requires every actual saved root to have a genuine adopter. Failure cleanup destroys unadopted QMON trees and worlds before immutable inventory staging or resource/material/VFS owners retire. QMON destruction releases stable model/animation tokens after dependent children. Actual adopted consumers, including Q3 rows and visual caches, keep their own tokens/resource versions and child-first teardown.

Installed decoded inventories must not retain or reuse a transient content graph. Capture always uses a new graph and current genuine producer getters, including still-live token provenance. Pure ordinal lookups/adoption checks must not allocate, acquire content, mint namespace IDs or replay events.

## Q3 owned-versus-borrowed world prerequisite

q3_assets_owner owns the new `src/app/frontend/q3_inventory.c/.h` helper. It consumes the exact world_inventory.h callbacks. Parent supplies genuine audio/media assets and other actual owner rosters. Collision is the real parent world geometry; its Q3 helper uses only that existing physical owner and does not construct collision geometry.

Existing Q3AS world_decode serves both borrowed mapworld references and owned preview worlds, with no role argument. Pure decode alone cannot distinguish them. Existing model_fields checks owns_world against resource presence and provider heaps, but that does not prevent an owned mapworld claim with a matching tuple. A no-fail adopt callback cannot safely repair ownership after full stream completion.

Root authorized q3_assets_owner to add explicit scene_owned_ready/world_owned_ready callbacks to `qa_q3_asset_owner_refs` and call them for actual owned rows before full-stream adoption. They should forward to this helper's genuine Q3 scope-qualified readiness functions. Those additional callbacks were not present in the last inspected header/code, so their implementation/review remains a source-owner prerequisite. Do not substitute conditional no-op adoption, a mutable resolver claim counter, or a guessed callback phase.

## Current ownership and acceptance boundaries

- This agent's END-water2 is accepted and held at exact hashes above.
- This agent's immutable model inventory is accepted and released at exact hashes above.
- World inventory header is an agreed declaration draft only; no C implementation or acceptance exists.
- frontend_resume owns parent constructors/aggregate integration.
- q3_assets_owner owns Q3 helper and its root-authorized owned-ready codec correction.
- Source and wire owners retain genuine rawPS/FOLLOW/END core backing. The actual core definitions now exist, with independent acceptance separate.

Resume with source reads and explicit single-writer boundaries. This handoff records source/hash evidence only and does not claim complete frontend save/import or complete native Q3 pipeline acceptance.

