# Current B01–B05 source audit, 2026-10-01

Reviewer: independent agent `/root/audit_foundations`; runtime session `01a0fa01-c57e-7a30-97a4-7d785d731795`, verified model `gpt-6.1-sol`, effort `xhigh`. Jev session `s_8182c449b36448a9b4c48fdb3a434f99`, contribution `w_fb447571a49547f0a867de5d49a945fe` under current parent B34 work `w_2959dd54d5294d8a82e5d1fec94e2e5b`; plan 7 acknowledged. This report is in progress. It does not accept historical completed states or complete parent B34.

The acceptance authority is dependency plan revision 7, tasks B01–B05. Review is source-only: no configure, compiler, build, test, parser execution, project script, sanitizer, runtime, performance measurement, or Git mutation was performed. Root LICENSE and author metadata are preserved; donor source and assets are read-only.

The user's latest correction forbids review gates and test infrastructure in front of implementation. Root explicitly directs this audit to continue read-only alongside production installation, with only this existing document editable and no new manifests, hashes or freeze requirements. Historical snapshot/peer evidence below records work already performed; none of it imposes a prerequisite on current implementation. Concrete defects go directly to the author. Current owner/build files can change as genuine callers are installed.

## Original criteria and current limits

| Task | Original goal | Original criterion | Current audit status |
| --- | --- | --- | --- |
| B01 | Implement the C17 build, error and ownership types, checked binary access, file I/O, memory utilities, and native executable entry. | C17 build definitions, core implementation and native entry are written and source-reviewed; no Bun runtime dependency. Compilation and runtime validation are deferred to P01. | Whole foundation/build/entry source read; no confirmed defect in that scope. Current original-criterion judgment is still unavailable. |
| B02 | Implement shared PAK and ZIP/PK3/PK4 readers with ordered entries, normalized paths, stored/deflated extraction, and corruption checks. | Archive readers implement ordered extraction and malformed-input rejection; stored data is shared without repeated copying. Source review covers ownership and bounds; runtime validation is deferred to P01. | Whole native archive reader/header, extraction/retention VFS callee and whole donor archive implementation read; no confirmed defect. Original-criterion judgment is still unavailable. |
| B03 | Implement loose/archive mounts, precedence, content identity, caching, lifetimes, and writable overlays. | One mount/resource service resolves identities and shares immutable data across game families without merging distinct content. | Whole VFS/pool/view codec, all six FS6 files and catalog discovery/mount/clone/write/save callers read. Lower FS6, WritableProvenance6 and VFSIssued2 historical source observations are reconciled. PortableWrite3's changed host files/private/checkpoint were freshly read whole as real constructors/importers are installed directly. F-B03-01's actual QARS4/QAVF6 history repair is independently corroborated through complete revised owners. Complete decoded-resource/family/cold behavior remains under review. |
| B04 | Implement Q1/Q2/Q3 map formats and required variants, record readers, entity text, visibility, and extensions. | Donor-supported BSP variants have checked C readers; source records and extensions retain required semantics. | Whole current BSP reader/header and all Q1/Q2/Q3 map donor modules read; no confirmed reader defect. Original-criterion judgment is still unavailable. Moving world owners remain separately unaccepted under B16. |
| B05 | Implement model/image formats, palettes, sprites, animation data, replacements, and attachments. | All required donor formats have C readers and common retained resources; no supported format silently drops records. | Whole native and donor model/image reader families and donor common replacement/attachment/resource callers read. Amended FORMAT4's 1,449-line lower repair is independently source accepted, including F-B05-04. Whole Scene model owner corroborates its finite-only early guard; actual registration initialization now joins production callbacks. Complete common resources and cold topology remain under review. |

NativeView owner explicitly identified moving paths: `src/render/scene/resources.c`, `resources_internal.h`, `resources_state_save.c`; `models.c`, `models/internal.h`, `models/owner_save.c`; `world.c`, `world/internal.h`, `world/legacy/textures.c`, `world/legacy/internal.h`; material library/parser/save; `include/qa/scene.h`; frontend visuals/access/shared-resource policy/bindings/native_q2/map_events. Their aggregate source behavior remains unaccepted by this report, and implementation proceeds while it is reviewed. NativeClient's earlier accepted lower FS6, WritableProvenance6 and VFSIssued2 snapshots are reconciled below.

NativeView's earlier owner report identified first registered-model replacement constructor admission as a separate unassigned producer gap. Its registered-parent helper and NativeCGame's ModelOpening4 metadata did not themselves supply the receiver. Production receiver installation has since landed; the historical omission witness below must not be treated as current absence.

The missing constructor consumer was originally source-confirmed through whole `src/presentation/q3/models.c` 327 lines, `assets.c` 289, `internal.h` 145, public `q3_presentation.h` 266 and `q3_model_opening.h` 27. At that revision ordinary non-MD3 decoding directly returned `qa_scene_model_create`, asset options lacked an initialization receiver, and the initializer symbol had only a definition and declaration. Whole donor `q3-client/assets.ts` 145 delegates registration to `ApplicationAssets.model`; whole `assets.ts:329` calls the 115-line `model-loader.ts`, which prepares and commits a supported replacement before returning the model. This established the original behavior gap without a runtime claim.

Fresh current whole `src/presentation/q3/models.c` 347 now invokes `options.model_initialize` for each distinct decoded scene after retaining its first request/opening and before handle publication. Whole current `visuals.c` 1,053 provides the actual policy/replacement consumer and retained parent lease path. A fresh complete symbol search finds production callbacks in `source.c`, `native_q3_client.c`, `remote_q3_client.c` and `remote_q3_initial.c`; their corresponding asset options supply the receiver. Callback bodies check their real source/native/remote parent before and after consuming the initialization. This removes the earlier missing-symbol/receiver witness. Complete current enclosing source/service/cold owners remain under review; these actual new source joins do not imply aggregate execution acceptance.

## Concrete findings and lower repair acceptance

### F-B05-01: the public Q3 PCX profile rejected a donor-defined zero-length packet — lower repair accepted

`src/formats/image/indexed.c:68` rejects a PCX packet whose RLE count is zero for both public profiles. In donor `src/formats/images/q3-pcx.ts`, `decodePcxIndexed` consumes `0xc0`, consumes its following data byte, performs a zero-length fill, and proceeds to the next packet. A valid Q3-profile 1×1 header, a `0xc0, data, pixel` payload at byte 128, and the final 768-byte palette therefore reaches one decoded pixel in the donor and fails in C. This is a bounded public `QA_IMAGE_Q3` decoder contract mismatch. The ordinary format profile's rejection is appropriate.

Caller qualification: the actual donor common scene texture loader imports ordinary `decodePcx`, and the current native common scene uses the ordinary PCX profile. The Q3-specific donor export has no production caller found in Anthology source. This finding does not claim an observed gameplay failure or an incorrect common-scene dispatch.

### F-B05-02: the public Q3 BMP profile accepted an empty sub-8-bit header that the donor drops — lower repair accepted

`src/formats/image/raster.c:172` publishes an empty Q3 image before checking unsupported depth at line 175. Donor `src/formats/images/q3-bmp.ts` dispatches `bitsPerPixel < 8` through `BmpDropError` before its zero-output early return. A complete uncompressed Q3-profile BMP header with matching declared file size, zero width or height, and depth 1 or 4 is rejected by the donor and succeeds in C. Move the sub-8-bit rejection before the empty-image branch while preserving donor-defined empty 16-bit/other depths.

Caller qualification: the common scene loader imports ordinary `decodeBmp`; the native common scene selects the ordinary BMP profile. No current production caller of the public Q3-specific donor BMP export was found. This is a bounded public decoder contract mismatch, not a claimed production rendering failure.

### F-B05-03: non-MDL model sampling rejected finite donor extrapolation — lower repair accepted, Scene guard reread

`src/formats/model/model.c:234` rejects every `back` outside 0..1 before selecting the format. Donor `q3-model/md3.ts:303` validates finiteness and permits extrapolation; MD4 `skinMd4Surface` and MD5 `sampleMd5Pose` have the same finite-only selection contract. Donor common-scene MD2 `interpolateSceneMd2` also performs finite extrapolation. A valid two-frame MD3 with `back=2` produces the donor's extrapolated geometry and returns `QA_ERROR_ARGUMENT` from the native public sampler. Native MD2 alias sampling at `model.c:295` and MD5 animation sampling at `md5.c:533` impose the same unsupported narrower range. Preserve the explicit 0..1 contract of donor MDL `interpolateAliasFrames`; loosening every family would be incorrect.

The pre-repair native Scene caller `models.c:911` rejected the value before frame repair/replacement and called the lower sampler at lines 734–749. Actual Q3 host `qa_q3_host_ref_entity_decode` retains the guest field at `presentation.c:31`. NativeView acknowledged that it has no intentional narrower-range source witness and subsequently reported its finite-only Scene guard amendment landed, leaving genuine MDL admission to the actual sampler selected after frame repair and replacement. Its caller is being reviewed alongside the moving owner's implementation. This finding is a source behavior difference; no executable or gameplay run was performed.

The actual current 1,056-line Scene model owner has now been read whole, with its private 92-line header and 646-line owner codec. The early guard accepts finite `back_lerp`; replacement selection and frame repair precede the selected lower sampler. This independently corroborates the narrow Scene guard repair. It does not complete first constructor admission, material bindings or the enclosing aggregate.

The preceding witnesses describe the pre-repair source. CommonSource's frozen FORMAT4 packet was independently reread whole here: `indexed.c` 238 lines, `raster.c` 256, `model.c` 373, `md5.c` 580, total 1,447. The complete public/private image/model contracts and real image allocation/decode helper were freshly read, as were the full ordinary/Q3 PCX/BMP, MD3, MD4, MD5, quaternion, alias-animation and actual Scene prepare/textures donor owners. Any truncated batch was refilled before acceptance. F-B05-01/02 are resolved in this bounded lower source packet. F-B05-03's lower sampling guards are resolved: actual MDL remains strict 0..1; other sampled families accept finite interpolation/extrapolation. MD5 positions extrapolate while quaternion selection returns current orientation at `back <= 0`, prior orientation at `back >= 1`, and retains the current joint scale. The moving Scene caller's amendment is reported and its complete surrounding consumer behavior remains under review. This acceptance neither accepts broad B05 common owners nor supplies P01 execution evidence.

### F-B05-04: equal-frame MD3/MD4 sampling can change a pose under finite extrapolation — lower repair accepted

Further refutation of FORMAT4 found a valid-input identity case in `qa_model_sample_mesh`: MD3 and MD4 evaluate the interpolated positions/matrices even when `frame == old_frame`. With a finite binary32 `back = FLT_MAX` and a valid current coordinate or bone-matrix coefficient of 1, `1-back` rounds to `-FLT_MAX`; the native expression cancels the two terms to 0. Whole donor MD3 `interpolateSurface` returns the current frame directly when the frames are equal; whole donor MD4 `skinMd4Surface` selects `back = 0` in that case. Its unchanged pose therefore retains 1. MD5 already skips equal-frame blending. Actual Scene MD2 deliberately keeps same-frame interpolation, so a blanket all-format shortcut would erase its donor behavior.

The current Scene donor and native frame repair set MD3/MD4 equal-frame lerp to zero, so this is a bounded public lower sampler witness, not an observed gameplay failure. The earlier FORMAT4 acceptance was withdrawn for `model.c` and the four-file aggregate while this witness was open. CommonSource subsequently refroze `model.c` at 375 lines; it now sets `back = 0` only for equal-frame MD3/MD4 after actual frame/output admission. This complete final owner and the complete MD3/MD4 donor files were freshly reread without truncation. The repair resolves the cancellation witness, preserves genuine MD2 equal-frame arithmetic, and leaves MDL's strict range and MD5 unchanged. The other three accepted files retain their hashes. Amended FORMAT4 is independently source accepted at 1,449 lines; the moving Scene caller and broader B05 resource/cold topology remain unaccepted. No script or numerical execution was used to derive the witness or review the repair.

```text
d821fa3af0690afbc387b8dd12920d3bc661d8c8480692e38014998f84715c3d  src/formats/image/indexed.c
d79075afadb26682dc4cde5d903c46cf12cc4b4fff5ab11c611e7ee797cd30ac  src/formats/image/raster.c
ff8e024b4c2212f49549571b4f52be70e925d604866cbcfaee4705947c8a2814  src/formats/model/model.c
c439c61cd86444bb1753bceabdb4f16b0b8f0a6188e8af1f2c4802f5b841edf0  src/formats/model/md5.c
```

### F-B03-01: a retained image cache cannot capture a retired source mount — actual repair source corroborated

The pre-repair whole `resources.c` 1,363 lines, `resources_internal.h` 43 and `resources_state_save.c` 440 exposed a valid lifetime witness. `qa_scene_image_load` creates an immutable image and `cache_add` retains its actual content resource and genuine source/logical mount IDs. The public VFS contract permits retained resources to outlive `qa_vfs_unmount`; removing a mount does not destroy that image or its retained bytes. However, the pre-repair QARS `content_field` at `resources_state_save.c:100` searched only the current mount inventory, required `found`, and encoded the current scope ordinal. Loading a valid image, keeping its bank, then unmounting that image's source produced a genuine retained cache that still supported its immutable image but could not checkpoint. The same limitation applied to an independently retained logical-size source.

This was a bounded public resource-continuation witness, not an executed remount/gameplay result. NativeView confirmed no universal production lifetime guarantee that current VFS membership outlives every image cache. CommonSource and NativeView then installed genuine issued-mount/opening provenance directly; the complete revised owners are described below.

The preceding witness is historical after the direct production repair. Whole revised `resources_state_save.c` 455, `vfs.c` 2,188, public `vfs.h` 291, `vfs_private.h` 99, `vfs_view_save.c` 538 and public `vfs_view_save.h` 32 were independently read in full, total 3,603 lines. Every successful real acquisition records the genuine issued mount ID and pool-local resource origin. Clearing the live journal and unmounting preserve that history; clones own copied receipt paths. QAVF6 serializes the retained resource's original native/archive provenance and does not reopen a retired mount. QARS4 captures/restores the historical issued ID through `qa_vfs_resource_origin_read`, with the exact resource pool/version, path, digest and archive member checks. Legacy QARS2/3 decoding and QAVF5 read-journal origin reconstruction remain available. This resolves the retained image/logical-source after unmount witness in current source. No additional bounded defect was found in those complete reads; complete family/module/media/cold execution is not inferred and no new manifest, checksum or freeze requirement was introduced.

A fresh whole read covers the subsequent `resources.c` 1,399, private header 44 and resource codec 457. The actual exact-file loader bypasses image override and extension searches, and its cache key, public request receipt and policy replay retain that admission rule. QARS5 serializes the cache's `exact_file` flag; QARS2–4 decode it as false while preserving their original field order. The revised codec retains the repaired historical issued-origin checks. Public `scene.h` 693 and legacy world private header 57 were also freshly read whole.

The real new exact-file consumer was read whole: frontend `q1_sky.c` 279, policy owner 120, private/public headers 32/36, save owner 156 and save header 10. Its six-face loader explicitly tries TGA then PNG, retains genuine missing-image fallbacks when some faces succeed, and clears the faces when none do. Policy preparation loads against the corresponding actual candidate bank; restore resolves retained namespace images without loading a face. Whole donor `app/bootstrap/q1-service-presentation.ts` corroborates the six suffixes, TGA/PNG search, partial-face fallback and recipient/broadcast ordering. No additional bounded defect was found in these source bodies. The earlier complete symbol/build search found no external Q1 sky installation at that read. Production installation subsequently landed: a fresh whole 298-line owner includes actual retired actor/provider pruning and source `atof` fog handling, and fresh symbol searches identify real lifetime/original-frontend construction, map admission, sky command, shared policy, persistence checkpoint/restore/ready/destruction and CMake units. These actual joins remove the earlier absence witness; complete enclosing parent/namespace/cold source behavior remains the aggregate reader's moving scope and imposes no audit prerequisite.

### F-B03-02: failed remote Q2 cold candidates cannot retire — scoped source repair corroborated

Whole authored remote Q2 client 367, media owner 136 and cold codec 273 exposed a bounded failure-lifetime witness. `frontend_remote_q2_restore_prepare` linked its new row and set `importing = true` before decoding; a malformed payload or later allocation/admission failure returned false with the actual row still retained in `*out` and the frontend list. Both exposed destroy paths rejected importing rows, so the failure-owned candidate could not retire its claims, stage or media through its owner API. This was a source constructor/destructor contract witness; no installed parent cold failure or execution was claimed.

RemoteContent repaired the actual client owner immediately. The complete amended 376-line owner was reread, along with the complete 251-line download owner and the media cleanup callee. Destroy and destroy-all admit importing rows only while their actual frontend is `source_restoring`; ordinary importing and entered/busy guards remain. Partial null config allocation now clears safely. Destruction releases media children, parsed-model leases, retained content, the download stage/root and then private VFS/catalog ownership. This resolves the failure-retirement witness. Parent hold/shutdown and dictionary installation continue separately; adding the ordinary idle predicate to an enclosing cold cleanup guard must preserve this actual isolated importer path.

The same whole client read also identified an exact domain comparison omission: the amended equality compared command actor slot/generation but omitted its registry, even though the cold codec saves the full `qa_actor_id`. The author replaced that comparison with actual `qa_actor_id_equal`; its real primitive compares registry, generation and slot. That narrow final member repair was directly reread. This is domain qualification evidence, not remote gameplay or complete cold acceptance.

### F-B03-03: remote Q2 downloads later assets before rejecting the installed map checksum — actual source repair corroborated

The pre-repair whole `remote_q2_download.c` 251 and donor `app/bootstrap/network/q2-downloads.ts` expose a concrete acquisition-order witness. The donor resource generator yields the map first, then opens the retained mounted map and compares its advertised block checksum before yielding any model, skin, sound, image, player, sky or texture request. Native `remote_q2_download_prepare` requested the map and immediately walked those later assets; checksum validation ran only in `remote_q2_media_prepare`, after download preparation finally became ready. With an already installed map, a mismatched advertised checksum and a missing valid model path, native preparation opened a download stage and sent that model download before it rejected the bad map. The donor rejects the map before those side effects. RemoteContent and Root were notified immediately. This is an actual source preparation/callback-order witness; it is not a BSP-reader defect or an executed acquisition/gameplay claim.

Whole revised download owner 267, media owner 144 and private interface 91 were reread, with the complete 368-line donor acquisition owner. The real download preparation now acquires and admits the installed map through `remote_q2_map_validate` before requesting later assets. A missing map still waits for its genuine download, and final media construction repeats validation on its own retained opening receipt. The reported downstream-download side effect is removed. No additional bounded defect was found in that repair. Donor HTTP metadata, filelists, package publication and cancel/retry remain separate outer acquisition behavior; this source repair does not complete that original workflow or impose a review gate.

### F-B05-05: shader CIN/OGV initial registration conflates the initial binding with decoded publication — source repair corroborated

The pre-repair production shader callback at `frontend/material_movies.c:131` constructed a cinematic and called the real registry's `qa_material_movies_add`, which called generic `qa_cinematic_image` at `src/media/material_movies.c:60`. Complete CIN playback construction immediately reads frame 0 through `cin_playback.c:75`. Complete OGV construction immediately calls `decode_frame` at `ogv.c:293`, producing an opaque alpha channel. The pre-repair generic image publication only created an asset-sized transparent initial surface when `!movie->has_picture` at `cinematic_scene.c:39`; for CIN/OGV it instead copied the already decoded picture. However, actual QFMM `row_valid` requires every successful retained initial image to contain only zero bytes at `frontend/material_movies_save.c:73`. A valid supported CIN/OGV `videoMap` with an opaque first frame therefore registered but could not checkpoint through that real owner.

Whole donor `ApplicationAssets` 361, `media/playback.ts` 196, `media/presentation.ts` 167, `media/material.ts` and `render/scene/resources.ts` corroborate the separation: shader registration allocates identity/dimensions before constructing playback, and the initial binding receives no pixel upload until the reached `MaterialCinematic.resolve`. CIN/OGV playback already holds a decoded picture, but that does not upload the shader binding during registration. The donor allocator itself does not allocate zero pixels; native QFMM's transparent initial receipt is its explicit representation of that separate binding. Ordinary seat/system initialization is a separate contract. A repair must also preserve the first reached decoded publication when its revision is still zero; assigning the initial binding that same decoded revision without a separate pending state would suppress the upload. Root, NativeView and AuditRendering were notified immediately. No movie was executed, no new test infrastructure or review gate was introduced, and no source repair is made by this readonly lane.

MaterialMovies subsequently repaired the real lower registry and publication owner. Whole revised registry 130, cinematic scene 193, QACP codec 85, internal header 35 and material-image seam were reread independently, with the real complete QFMM owner 366. Shader registration now uses a material-only initial producer, creates the transparent asset-sized immutable image even when CIN/OGV already holds decoded pixels, and retains `image_revision = UINT64_MAX`. The first reached generic publication therefore admits decoder revision zero and replaces the existing same-sized image through its actual immutable lineage. QACP accepts this sentinel only for the genuine material target and verifies retained asset dimensions and zero pixels. Ordinary seat/system generic eager publication remains unchanged. This resolves the reported initial registration/capture witness in source; enclosing media/cold execution is not inferred and no new review gate was introduced.

### F-B05-06: Source name-cache reuse could bypass an exact-file request — actual source repair corroborated

The complete 1,433-line resource owner and 462-line QARS owner exposed a public API interaction during the Source upload amendment. The new ordinary Source name-only fast cache ran before the exact-file branch and did not check either request or stored `exact_file`. An ordinary Source request for `textures/foo.tga` that selected a PNG override could therefore satisfy a subsequent exact-file request for that same name with the cached PNG. Public `qa_scene_image_load_exact` explicitly requires the named file and no override/extension search. QARS's new Source name-only duplicate rule also treated an ordinary and exact receipt as the same key. No current Source-profile exact-file production caller was found: the actual Q1 sky exact consumers use ordinary Q1 options. This is a bounded public resource-contract witness, not a claimed gameplay failure.

NativeView repaired both actual branches immediately. The fast cache now requires `!exact_file` for the request and stored row; QARS's Source name-only duplicate rule requires both rows to be ordinary. Those final changed branches were directly reread after the complete preceding owner reads. Authentic ordinary Source first-name cache behavior is preserved. No additional complete final-owner read or enclosing cold execution is inferred from this bounded repair check.

### F-B03-04: the moving QAVF7 writer temporarily rejected its own version — actual source repair corroborated

During the historical-opening amendment, the actual view writer emitted version 7 while `view_fields` still admitted only versions 5 and 6. Its own checkpoint therefore rejected the new header. This in-progress producer contradiction was sent directly to Root and the author; it imposed no gate on implementation. NativeView owns this historical-recipe extension and repaired the admission to versions 5 through 7. The complete subsequent 587-line codec was read without truncation and joins the new historical rows only for version 7. No current version contradiction remains in that read.

### F-B05-07: material capture rejects a genuinely retained per-registration Source UI flag — actual source repair corroborated

Fresh whole material library 1,680/private 114, library codec 656, record codec 201 and actual frontend Q3 render-policy producer 195 exposed the pre-repair capture-admission contradiction. Source registration samples the genuine current UI flag into each new record at `library.c:1013`; Source image-policy preparation intentionally preserves that record's admitted flag at line 684. The real callback reads canonical `r_uifullscreen`. However, the pre-repair `library_save.c:229` required `same_profile(material.profile, library.profile)`, and line 188 included `ui_fullscreen` in that equality. A valid Source library with records admitted under different UI values, or an existing record preserved across a global UI profile change, therefore failed actual checkpoint or restored graph admission. The record codec already serializes each record's own flag. Ordinary global profile admission should remain strict while genuine Source per-record UI differences are admitted. Root and NativeView received this concrete witness immediately; implementation continues without a review gate. No checkpoint or gameplay was executed.

The preceding witness is now historical. NativeView amended the actual codec comparison to permit only `ui_fullscreen` differences when the library is genuinely Source-tagged, while comparing every other profile field and retaining strict ordinary UI equality. The final changed comparison and `library_valid` call were directly reread after the complete codec read. This resolves the reported per-registration UI admission witness; no aggregate cold execution is inferred.

### F-B05-08: Source builtin owners were omitted from the new resource continuation — actual source repair corroborated

Whole resource owner 1,488/private 47 and QARS6 owner 462 exposed the next actual producer/continuation seam during builtin installation. Live Source initialization retained white/default/identity images, its `source_builtins` flag and their upload recipe, and Source loading/material binding consumed those actual pointers. QARS6 encoded none of those newly added fields and restored none of the pointers. A detached restored Source library with more than two records then entered `set_source_upload`, found no Source missing image and failed its fresh-only `count <= 2` initialization branch. NativeView and Root received this actual continuation omission directly; no runtime or complete cold caller claim was made.

The complete subsequent QARS7 owner 517 was freshly read without truncation. It retains the three genuine namespace image references, the admitted builtin flag and actual upload recipe; verifies their native image descriptors/pixels against that retained recipe; transfers the saved owners without creating new builtin images; and releases partial failure references. Legacy QARS2–6 field order remains unchanged. The new shared material producer binder was also read in the complete current 203-line frontend Q3 render-policy owner. The omission is removed in current source. Enclosing display/color/module/cold installation remains separate moving work.

### F-B05-09: a fresh replacement acquisition reused an older historical rank — live and remote codec repairs corroborated

The complete revised VFS and Visuals reads exposed a genuine fresh-versus-retained opening distinction. `vfs_history_record` deduplicates resource ID, mount and complete path/link tuple and deliberately preserves that tuple's first-ever rank across clear/order changes and clones. Visuals `replacement_read` acquired a new MD5 mesh, but its `opening_rank` selected that older historical rank. With original order `[A,B]`, prime a supported MD5 mesh present only in A at rank 0; then select order `[B,A]`, where a native MD2 present only in B first registers at rank 0 and the freshly opened mesh in A has rank 1. Historical selection still returned mesh rank 0 and admitted the pair. Whole donor `model-loader.ts` selects each actual `mesh.reference.resolution.rank`, and `md5ReplacementAllowed` rejects 1 > 0. Whole donor mount opening/order reader constructs new order-qualified references over the retained physical sources.

NativeView independently confirmed this source witness and added an owned actual opening snapshot to genuine acquisition receipts. The actual new capture/dispose/codec bodies were directly read: successful acquisition captures its current order/rank independently of the first-read journal, disposal releases its owned order/prefix, and the pure continuation validates issued mount IDs, unique order members, selected rank/mount and link/prefix rules without reacquiring. The final empty-receipt guard covers every new snapshot field. Visuals' fresh replacement and native initialization/policy consumers now prefer this actual acquisition snapshot; Q3's opening producer does likewise. This resolves the reported fresh replacement admission witness in the directly read bodies.

Fresh whole RemoteQ1 restore 478 and RemoteQ2 restore 423, their private/restore headers and actual live media owners 140/149 corroborate Q1RC2/Q2RC2 snapshot continuation for genuine map and model receipts. Imports resolve the retained VFS and preserve each constructor snapshot without acquiring content. The remote cold omission therefore no longer appears in those source owners. Canonical borrowed QC model presentation still selects historical rows in the last directly read Visuals revision; NativeView and Root are installing its actual precache receipt field and corresponding QC codec join. That separate borrowed continuation remains moving. These observations do not imply aggregate cold execution, and implementation proceeds without a review gate.

### F-B05-10: RemoteQ2 saved parsed-model keys used the wrong index convention — actual source repair corroborated

The complete 423-line RemoteQ2 restore owner and its actual parsed inventory encoder exposed a producer/consumer mismatch. `frontend_model_encode` emits zero-based inventory index `i`. Q2RC2 wrote that value unchanged, while importing admission required a nonzero `saved_model` and root attachment queried `saved_model - 1`. A genuine first inventory row therefore failed restoration; later rows referred to the preceding parsed model. RemoteQ1's corresponding owner already explicitly converts its parsed key to the one-based saved convention.

RemoteContent confirmed and repaired the actual Q2 writer immediately. Its final changed branch was directly reread after the whole owner read: following successful actual parsed/root encoding, it rejects `UINT64_MAX` and increments the parsed key before serialization. The original one-based readers are unchanged. This resolves the index-convention witness in source. Root and the author received the concrete defect directly; no checkpoint was executed and no review gate was imposed.

### F-B05-11: newly installed Source image recipients still selected ordinary PCX/BMP profiles — actual source repair corroborated

The actual new Source material upload producer tags `options.source_q3`, and the real parser retains that flag while supplying its image request to the common resource loader. The preceding loader still explicitly selected `QA_IMAGE_FORMAT` for PCX and BMP. Whole freshly reread Q3 PCX/BMP donors 85/84 and the previously complete original `tr_image.c` establish defined recipient differences: the zero-run PCX witness from F-B05-01 is admitted by Source, and a supported 1×1 24-bit BMP with 54-byte header plus three tightly packed pixel bytes is admitted by Source while ordinary row padding requires four bytes and rejects it. These are actual newly installed Source-provenance recipients; the earlier qualification that ordinary common Scene callers use ordinary profiles remains correct. The source-indeterminate nonempty 16-bit BMP path is not a supported-pixel witness.

NativeView repaired the actual two dispatch branches immediately to choose `QA_IMAGE_Q3` only when `options.source_q3` is true. Both final branches were directly reread; ordinary common Scene PCX/BMP behavior and the upload tail remain unchanged. This resolves the reported Source recipient mismatch in source without any executed rendering claim. Root and the author received the finding directly, with no review gate.

## Whole-file native coverage already read

Core foundation: `include/qa/{common,arena,binary,hash,strings,text,tokenizer,json,json_writer,common_parse,math,source_number,q3_key}.h`; `src/core/{common,arena,binary,hash,strings,text,tokenizer,number,number_js,json,json_writer,common_parse,normals,q3_key}.c`. Platform whole-file reads: `src/platform/{file,file_windows,mapping,filesystem,filesystem_posix,filesystem_windows,native_runtime}.c`, `filesystem_internal.h`, `include/qa/{filesystem,native_runtime}.h`. Native entry `src/main.c`; whole `CMakeLists.txt`, `cmake/Native.cmake`, `NativeGuest.cmake`, `NativeBundle.cmake`. The initially read CMake/NativeGuest revisions were 1,495/183 lines. The subsequently identified 1,503/187-line versions were freshly reread whole; Root ACKed that historical read while continuing direct source installation. A fresh complete build-definition read now covers CMake 1,600 lines, Native 143, NativeGuest 193, NativeProfile 53 and its genuine client subproject CMake 40. These definitions select C17, link the actual native content/frontend/helper targets, and register the profile client and installation dependencies without a Bun runtime dependency. No configure or external dependency fetch was performed, and this build-definition read does not accept the B24 profile implementation or other gameplay targets. The recipe can continue changing as production units are installed. Core script and actual console/profile bindings remain the B21 reader's scope; reading q3_key is supplementary and does not accept its B21 consumers.

Archive/service: `include/qa/archive.h`, `src/content/archive.c`; `include/qa/{vfs,vfs_save,vfs_view_save,catalog,catalog_save,catalog_write}.h`; `src/content/{vfs.c,vfs_private.h,vfs_save_io.h,resource_pool_save.c,vfs_view_save.c}`; whole `src/content/catalog/{internal.h,catalog.c,discovery.c,mounts.c,products.c,metadata.c,behaviors.c,q3_mods.c,q3_product.c,q3_write.c,save.c}`. Final owner snapshot relation and full frontend/application cold topology remain unaccepted.

The new historical-opening continuation received complete current reads of `vfs.c` 2,267, public header 301, private header 106, view codec 587 and view header 32, with the complete save I/O helper. Real successful acquisition retains the complete request/lookup/link recipe, resource and issued mount IDs, first rank/order/prefix and overlay admission. Clearing references and unmounting preserve these weak resource-ID histories; clones own copied strings and orders, while getters omit resources that the actual pool has trimmed. QAVF7 serializes retained histories after genuine origins and preserves retired issued IDs without reconstructing current order. Legacy 5/6 imports build historical rows from their actually admitted saved live reads after native binding. No additional confirmed defect was found in these whole owners.

After the actual acquisition-snapshot amendment, the complete VFS owner 2,321/public header 309/private header 106 was freshly reread, refilling the one truncated batch before coverage was recorded. Successful receipt acquisition retains its own lookup order independently of deduplicated journal/history rows, and failure/disposal release that snapshot's owned strings/order. Clone/history/resource/native and writable ownership remain joined to the complete lower owner. F-B05-09 records the concrete consumer repair and its still-moving borrowed QC continuation; no additional lower defect was confirmed in this whole read.

The actual lower writable host caller `src/compat/q3_host/files.c` (548 lines), its private header (35), and `include/qa/q3_host_files.h` (17) were also independently read whole. Q3 open/read/write/tell/seek/close dispatch preserves the actual per-stream source zip flag, including PK3-only zip seek behavior and the donor-defined repeated non-zip `SEEK_CUR` advance. The following hashes identify the accepted FS6 snapshot; the current body's subsequent PortableWrite3 amendment is identified separately below:

```text
58a4f536a1bcd4c8d3b94b8023444e8bb08e29cd6472f6f011e94a58b33420bb  include/qa/q3_host_files.h
5567f6ed43d2d959ec0276710066cb94d033d97f8f0a922b21a4b5c7bd2465af  src/compat/q3_host/files.h
07d9ad1b39e19e5c9bcef77f4d33cccd6678e075770f59bcb03da9e1416d176d  src/compat/q3_host/files.c
```

BSP: `include/qa/bsp.h`, `src/formats/bsp.c` whole file. Models: `include/qa/model.h`, `src/formats/model/{internal.h,model.c,alias.c,md3.c,md4.c,md5.c,text.c,metadata.c,scales.c,lod.c,transform.c}`. Images: `include/qa/image.h`, `src/formats/image/{internal.h,image.c,indexed.c,raster.c,png.c,jpeg.c,gif.c,wad.c}`.

New Source upload/color owners were read whole: `q3_upload.c` 138, current `q3_color.c` 87, public `q3_color.h` 55, image-options save helper 23, frontend `q3_color_policy.c` 344/header 31, current Scene public header 728 and resource private header 44. The complete original Q3 `tr_image.c` 2,520 and complete donor mip helper were read. Source power-of-two resampling, reduction before light scaling, same-size nonmip bypass, alpha preservation, simple/weighted mip selection, the weighted one-dimensional prefix behavior and mip tint arithmetic were traced through their real lower bodies. The new frontend owner binds the actual physical renderer/gamma capability and canonical rows, retains prepared upload values, and imports its saved color recipe without initialization or native writes. No additional confirmed bounded lower upload/color defect was found. Complete native gamma/display/video/cold callers remain separate source work; no executed pixel or native display fidelity is inferred.

Additional complete native scene model callees were read: `models/images.c` 218 lines, `lighting.c` 72, `shadows.c` 98, `sprites.c` 86, `topology.c` 116, `draw.c` now 284, and `resources_save.c` 233, total 1,107 current lines. The complete 209-line native 4,096-value `alias_shadedots.inc` table was also read. NativeView ACKed the five non-draw model callees and immutable-image codec stable outside its moving owner packet. RenderControls initially ACKed `draw.c` 289 frozen, then reopened it for AuditRendering's confirmed original-material sort admission finding. It repaired and refroze the 284-line owner, freshly reread whole here; AuditRendering's independent final confirmation remains separate. The draw hash below identifies this amended revision. The moving common model/resource structs, policy owner, material providers and complete cold topology remain separately unaccepted; these helper reads do not accept their aggregate callers.

Current common model/resource owners were subsequently read whole alongside implementation: `models.c` 1,056, `models/internal.h` 92, `models/owner_save.c` 646, `resources.c` 1,363, `resources_internal.h` 43 and `resources_state_save.c` 440, total 3,640 lines. Public `scene.h` 648, `scene_model_save.h` 101 and `scene_resource_save.h` 32 were also read whole. Material owner coverage now includes whole `library.c` 1,557, `library_internal.h` 109, `parser.c` 823, `library_save.c` 653, `library_save_private.h` 10, `record_save.c` 199, `order.c` 424, `internal.h` 21, public `material.h` 203, `material_library_save.h` 65 and `material_save.h` 24. These reads trace actual cache/content leases, replacement children, material callback receipts, private policy preparation and retained publication/order identities. Output truncation in an initial model-codec batch was refilled before whole coverage was recorded. F-B03-01 was found in the actual current resource codec; no broader common/cold completion is inferred.

Additional complete common caller reads cover frontend `visuals.c` 1,053, `visual_access.h` 39, `shared_resource_policy.c` 479/header 45, `resource_bindings.h` 23 and `material_movies_prepare.c` 159/header 19. Whole Scene `world.c` 1,396, `world/internal.h` 112, `world/legacy/internal.h` 55 and `world/legacy/textures.c` 239 trace actual material/image publication, sky and legacy binding ownership. Public `scene.h` 659, `material.h` 210 and `q3_presentation.h` 276 were subsequently read whole after reached Source scratch and registration receiver changes. These are observations of real source owners during installation, with no new freeze or checksum requirement; the new Source scratch implementation and complete production/cold media descendants remain separate joins.

Additional real B03/B05 recipient reads cover frontend resource constructor 68 and the authored RemoteQ2 client 367, then complete amended 376, media 136, download 251, cold codec 273 then complete amended 320, presentation 300, public/private interfaces 101/87 and restore interface 31. The actual client retains its domain catalog and selected private view; media teardown precedes that view's retirement, and the cold owner imports qualified retained opening/model/scene/image identities rather than replaying media acquisition. The earlier symbol search found only the real frontend destroy-all consumer. A new actual RemoteQ2 Source owner 210/header 29 was then read whole; it creates the real child after constructing its actual private CLIENT registry and lease-held launch metadata, applies real source configuration, and retains callback/console/metadata ownership through retirement. This removes the earlier missing immediate constructor witness. Its outer source/network factory and complete cold consumers still require their current assembly. Initial ordinary RemoteQ2 MDL/MD2 replacement selection and actual dictionary/policy/hold roster installation remain production joins: the observed model cache constructs a scene directly without current replacement policy, and the corresponding aggregate rosters did not yet enumerate RemoteQ2. These concrete joins were sent to RemoteContent alongside implementation; this read makes no full remote protocol, input, prediction, rendering, audio or cold acceptance claim.

The revised RemoteQ2 Source 243/header 40 was freshly read whole after actual parent-owner retain/current helpers landed. Its physical CLIENT tuple is checked against the retained metadata, registry, console and complete command actor identity; those checks read the genuine owner fields. Whole `client_registry.c` 302/header 53 and `session/configuration/transaction.c` 989/private header 22, with public `launch_q2_client.h` 20, trace the real constructor callees. A CLIENT registry retains its metadata and callback context, imports the matching physical registry once, and shares aliases through actual cvars/storage ownership. Q2 CLIENT metadata retains a selected private view and native role descriptions without preparing a GAME actor; restore admits the supplied claimed view without replaying discovery or acquisition. No additional bounded source defect was found in those whole owners. Their enclosing factory, remote protocol/rendering and complete aggregate cold installation remain separate moving work.

```text
39b8095f82cfd04fda33a9a022328df052db740e8e38854423734ccb0c58c30c  src/render/scene/models/draw.c
ab4b8049eaa21e62c67eecffc36311c2e8681a1ac069c07518e731b414a55d6e  src/render/scene/models/images.c
3e8766d3fe042b40daebf8d0940b113dd7c1f8b7ef29d1abe41b2e5f67b33123  src/render/scene/models/lighting.c
4b56d01ea6b3896f68908fbfd0868dd95b49680b3f1b8de1f384573a5702e484  src/render/scene/models/shadows.c
f5f463f53fb0c648d977a611c79b1c3d661b4b511ce8613459978432cd29e66d  src/render/scene/models/sprites.c
8533e964d70481adb399cb4eec906012416b79323eb2efb8b5595721b4740925  src/render/scene/models/topology.c
639a8b25df978fb11d8079cc05a23ae18640a10c9a5bafa4b8d25778cbb96717  src/render/scene/resources_save.c
3a8e1ae20d03e8cfadac368e1dea82ffc131619f872dcc5d5e0585399fd31e7d  src/render/scene/models/alias_shadedots.inc
```

## Source evidence from the stable readers

Archive PAK and ZIP-family readers retain directory order and duplicate entries. Bounds checks cover headers/directories/member ranges, local/central relations, descriptor fields, compression output, CRC, and rollback. Stored member data borrows the retained archive allocation; inflated data owns its allocation. PAK first-match and ZIP last-match member behavior is implemented by the VFS, with an ordinal-specific cache. Donor-supported restrictions on ZIP64, encryption, methods and multi-disk archives remain explicit.

The VFS uses native admitted file identity for loose versions and mounted package provenance. Packages compare full retained bytes and SHA-256 before sharing content; logical mounts retain their own admitted file/root and namespace. Resources retain package storage across unmounts. Views independently retain precedence, aliases, prefix orders, restrictions and read journals. Pool/view checkpoints serialize actual identities/topology/recipes, check duplicate or forged references, and admit candidates before replacing installed state. Whole POSIX/Windows source checks establish handle/root containment and identity admission, snapshot before/after checks, native writable stream positions/modes, atomic stage publication and platform-qualified root lineage. Catalog callers build family search orders from those retained mounts, use a separate maps prefix order for mixed geometry, clone admitted handles, preserve writable roots across Q3 media restriction, and validate restored member ordinals and actual writable lineage. These observations do not accept unread or moving frontend/application cold topology.

The BSP reader covers Q1 BSP29/BSP2/2PSB/Quake64, Q2 IBSP38/QBSP, Q3 IBSP44/46. Source record widths, typed data, entity pairs, texture metadata, visibility and extension bytes remain available. Hull validation includes all four Q1 headnodes, including hull 3. Q1 BSPX metadata and Q2 decoupled lightmaps, face normals and lightgrid octrees have checked structured readers. IBSP44 supplies derived shader/surface/model records through the retained reader rather than a fabricated converted file. Q3 unlit retail UV exceptions and flare fog exceptions retain donor semantics. Whole donor map files were read; integration into moving world owners still needs stable evidence.

The native model readers retain seven model formats (MDL, MD2, MD3, MD4, MD5 mesh, SPR, SP2), frame/skin groups, source GL command words, per-frame tags, skeletal animation components and scales, variable weights and every MD4 LOD. Bounds, count/stride multiplication, indices, truncated input and cleanup were traced through the complete native readers. Actual metadata/replacement/attachment consumers remain under source review while NativeView installs their common resource behavior.

Image readers retain indexed palettes/indices, mip levels, WAL/MIP metadata, WAD directory entries, GIF frames/delays/loop fields and PNG color metadata. The complete JPEG decoder uses native libjpeg, floating IDCT and explicit RGB/CMY output selection; donor JPEG and encoder were read completely, including progressive storage and Quake's overwritten K channel. Indexed expansion checks transparency before color translation. Linked-library execution equivalence and codec output fidelity remain P01 evidence, not a source-run claim.

## Provisional application and frontend joins

The subsequent complete Visuals owner 1,191/header 43 now consumes historical complete opening recipes for canonical, initial and replacement rank decisions, and includes genuine remote Q1/Q2 model-policy cache rows. The whole current RemoteQ2 media owner 149 calls `frontend_visual_model_opening_initialize` after real acquisition/decode/scene construction and before publishing its model cache row; its destructor retires the scene before decoded/source resource holders. This removes the earlier absent first-policy consumer witness for that cache. No current-order reconstruction or missing constructor receiver is asserted for these actual source joins. Complete corresponding remote dictionaries, model inventories and enclosing cold owners continue to change alongside installation.

Fresh actual visual/media reads now cover whole `visuals.c` 1,127 and access header 39, `shared_resource_policy.c` 506/header 46, material `library.c` 1,596/private header 110, frontend `material_movies.c` 300/header 51/private 45, save 366/header 24 and prepare 159/header 19. Lower owners read whole are media library 251/private 23/prepare 88/save 178 and prepare/save headers 17/22; material movie registry 129/private 20/prepare 104/save 111 and prepare/save headers 13/30; cinematic 652/private 35/scene 181/publication codec 83/public header 185/restore header 19/publication header 21/initial-image helper 18; CIN playback 179 and OGV 539. This complete set totals 7,286 lines. Actual visual constructors create and retain the movie producer before script/model registration; reached resolver binds the genuine submitted frame before playback. Prepared material publication keeps its original live callback; its private prepared callback destination is disposed before movie child retirement, which does not dereference the retired material ticket. Genuine path/failure receipts, native/archive resource leases and candidate rows are retained through their corresponding destructors. F-B05-05 is the concrete exception found. Complete current enclosing source/native/remote/Initial/system/audio publication callers and their lower decoder/codec fidelity remain separate aggregate work; no acceptance of those unread descendants is inferred.

Additional actual owners were read whole: `src/app/application/owner.c` 860 lines, `save_content.c` 769, `internal.h` 595, `save_content.h` 41, `lifetime.c` 367; `include/qa/application.h` 499 and `persistence_content.h` 43; frontend `image_inventory.c` 196, its header 17, `resources.c` 68, `remote_q3_initial.c` 260, its header 55, `restore_topology.c` 221 and `presentation.c` 230. This first caller/declaration snapshot totals 4,221 lines; InitialUI's subsequently changed source is separately identified below. The complete policy/resource/remote-media aggregate is not accepted by these reads.

The application ordinarily creates one shared resource pool, discovers catalogs against it, retains genuine baseline catalogs/pools, and claims restored pool ownership exactly once. The content graph inventories actual pool/catalog/view pointers and retained resource IDs, preserves executable opening receipts, checks the exact catalog-private view owner, and admits isolated pools/views/catalogs before exposing restored candidates. Claim flags transfer a destructor obligation once; the real application finalizer releases retained map/catalog owners before its pool and remaining graph. The frontend topology claims saved VFS owners and constructs detached image/material services; normal presentation clones the launch view, prepares image/material/world/audio owners before swapping, and releases retired services in dependency order. Their actual policy, source-group, guest and complete save/publication dependencies remain outside this provisional join acceptance.

Further whole provisional frontend reads cover `content_inventory.c` 112 lines and its header 10, `content_refs.c` 38 and its header 8, `root_restore.c` 86 and its header 10, `restore_topology.h` 14, plus `network_content_q3.c` 1,202 and its header 170: 1,650 additional lines. The visitor enumerates actual content owners, graph reference callbacks perform lookup without content acquisition, and root adoption preflights saved map/appearance rows before adoption. The remote content owner retains catalog/view source epochs, executable/map receipts, PK3 checksums/orders/references, writable roots and media journals; restore claims isolated catalog/view owners before exposing a pending candidate. Actual external inventory visitors, map/model adoption callees and enclosing network/application publication paths remain under source review while their production joins are installed. These reads do not establish aggregate cold topology or remote gameplay acceptance. RemoteContent ACKs its observed 1,202/170 metadata leaf unchanged and reports its actual external caller review ongoing; genuine remote-Q2 work is separate.

The whole genuine `application/publication.c` 1,363-line owner and `frontend/source.c` 2,211-line source-group owner, with `source_restore.h` 56, were subsequently read. Publication retains the actual map resource through BSP/collision construction, isolated restore and checked retirement; failed checked cleanup retains both snapshot rosters and their borrowed descendants for retry. Source groups clone genuine host views, initialize ordinary image policy before dependent materials/assets, retain actual role/configuration leases, and claim restored private views once. Restored group preparation uses already admitted graph resources; private map preparation retains its resource and the role/world exchanges preserve actual shared or private ownership. Frontend ACKed the source 2,211/content inventory/reference and root restore 86/header10 observations through those reads. Those historical coordination boundaries impose no freeze prerequisite on its new production changes. Complete actual shared policy, bank/presentation and external source callbacks remain open joins.

The whole current `application/providers.c` 1,373-line owner was additionally read. Its actual source preparation retains the launch metadata descriptor, parses selected QC/QVM artifact bytes, keeps the physical product catalog through constructor and restored-QVM paths, and releases descendants through checked family deconstruction. This reads the actual dispatcher, not every family/guest/startup callback it invokes. Those lower external callers and the complete save wrapper remain open aggregate joins; no provider/gameplay acceptance is inferred from this supplemental source read.

The complete actual `application/save.c` 1,807-line wrapper and `save_private.h` 54, public `persistence_application.h` 103, `frontend/persistence.c` 1,316 and its header 33, and `frontend/lifetime.c` 604 were then read: 3,917 additional current lines. The wrapper retains the active application/content leases, prepares an isolated graph and genuine selected-provider roster, claims retained views through the real launch callbacks, recaptures actual content identity before publication, and transfers checked-cleanup failures to a retained candidate. Frontend persistence inventories the genuine image/material/model/frame/audio owners, imports the admitted roots before their consumers, validates a fresh typed recapture, and publishes the guarded native holders at the application's publication boundary. Ordinary frontend retirement releases entered source callbacks before descendants and keeps failed shutdown owners for retry. Frontend ACKed persistence 1,316 unchanged/stable after Guest's bounded Registrar6 cut and lifetime 604 stable after bounded Caller3 acceptance; at that observation it excluded uninstalled remote graph, SystemCIN, audio/movie/music/storage/view envelopes and late finish hooks. Complete family/guest codecs and moving resource-policy consumers remain under review during installation. These whole caller reads do not establish aggregate cold topology or original B03/B05 acceptance.

The earlier InitialUI 260/header55 observation is historical. Frontend's current newly authored `remote_q3_initial.c` 346/header60 factory was freshly read whole, including its detached restore path, exact claimed view/pool relation, retained descriptor/registry, prepared child services and staged-attempt promotion. Its final author self-review, actual complete inventory codecs and aggregate caller joins remain pending. The old inventory omission is retained below as its original source witness, not assumed to prove the new factory or complete roster.

At the earlier 196/header17 QFIM/QFIS roster read, the separate InitialUI image owner was omitted; this corroborated NativeServerCommands' identified inventory gap. Fresh current whole `image_inventory.c` 211 and its 17-line header now include decoded remote banks as kind 7 and the genuine InitialUI parent bank as kind 8, using the actual network initial graph, with QFIM schema 3. The earlier omission is removed in current source. Its newly reached remote/initial graph readers and complete corresponding material/font/model/cold joins remain under review; no aggregate execution claim is made. AuditRendering separately reports whole stable native world geometry/lighting/patch/marks and donor scene/material reads, with no additional confirmed stable-reader defect; those reports do not replace this lane's own coverage.

Fresh whole `capture.c` 849/header 63 and `remote_q3_initial.c` 371/header 61, with `network_initial_graph.h` 18, now trace the actual decoded remote and InitialUI capture/resource-inventory roots. The roster includes their real assets, image banks, material libraries and fonts; metadata collection qualifies the installed InitialUI parent and its physical attempt/CLIENT heap under the resource fence. Capture retains assets before children and releases child holds before assets. The detached InitialUI import prepares its genuine claimed view and media services, keeps failed construction reachable for checked disposal, and separately promotes the completed restored attempt. These complete owner reads corroborate the installed roster and receiver joins. Their network graph bodies and full movie/audio/guest/cold descendants remain under review.

Whole current cold resource callers were then read: `material_inventory.c` 347/header 25, `font_inventory.c` 244/header 14, `q3_inventory.c` 850/header 49, `world_inventory.c` 771/header 89, `scene_identity.c` 719/header 127, `model_inventory.c` 768/header 82, `scene_inventory.c` 243/header 27 and `scene_refs.c` 154/header 26, with lower `presentation/q3/media_save.c` 199/public header 32, total 4,766 lines. Actual decoded remote and InitialUI material/font/asset/root rosters are present. Root capture records each genuine world/model destructor edge, private MDL subsets preserve parent allocation dependencies, restored parsed holders transfer independent content tokens to scene/cache consumers, and canonical recapture retains the installed physical identity relation. Stack resolver contexts are used during codecs; persistent content leases own their inventory row rather than borrowing the temporary resolver or construction graph. No additional confirmed defect was found in these complete owner reads. Fresh bounded `network.c` metadata/InitialUI graph bodies were also read; the complete 6,000-plus-line network owner and full module/media descendants belong to separate current integration reviews. These source observations do not establish aggregate execution or original B03/B05 completion.

```text
a7cbbcb53e5ee9dbda003929ba511606257fc0a8fff60d80aab91a5e56df6beb  src/app/application/owner.c
5bc00c3c99edbb527d527464a77b78d0898ea8f1fa6385f36c1d98ee60c2dfef  src/app/application/save_content.c
5e97a04defab6d4a6d5aae382aff8a0f92ed55e47f430f1412ecaf86532c6eb9  src/app/application/internal.h
39aa23fd7817fdb4aed43a3afc46e32ab5f54ad8c1527b9097e04faa799fe6a1  src/app/application/save_content.h
2a2fc5ba2db9b7ea75e50af617cfe0cbda451c75bb74ff7bcf3a5ccdad024624  src/app/application/lifetime.c
26878da7012f315f450c91770576144865edd3c11b3f6ce8b83538a30f64a6ad  include/qa/application.h
b613acd17c4ea1441842b437dae44978e82a0671f3b8f3c1307cdd984c0be0e0  include/qa/persistence_content.h
ba23e1e5faea8acc99decf65619c35f64f85b96c28bc7dc20c0e1e754f076938  src/app/frontend/image_inventory.c
b08633d98f810494adb435f5a89a17187b677c607a611d0554c4c19b21c77147  src/app/frontend/image_inventory.h
8ea3336e4373a1bf19ca9215babd9699450f7e9e90045e657d6032e96412dc93  src/app/frontend/resources.c
4163f249218a2e207beb6fcaa2f15448c9f272a530c95d1b57d0a4fd6511e5fb  src/app/frontend/remote_q3_initial.c
1e2b82ad96da3c8f982d13b55395ad4fc3b35bafe5620a5602ba21916ba200b3  src/app/frontend/remote_q3_initial.h
7a897822fb7518da82ac35f636cf3b4d34edcf08acb448a15ba3849a0f9461df  src/app/frontend/restore_topology.c
cab625868fde7feadf60698dd89a935288d877b856944c712ace67aac1a7a1d9  src/app/frontend/presentation.c
c2a36fd0c199cb68fa851027f2a615ed026e104193dff87ff9da88ccebadab5e  src/app/frontend/content_inventory.c
70665a16a16ea4e5be6751af9a107d44e0d798ba8836b6c3ecf1ee31c6e0958d  src/app/frontend/content_inventory.h
bcdcbbaa4990a0efb244c5def3cb27b98e16036f3a6e5075e2c69ef1d86cd3fc  src/app/frontend/content_refs.c
7e37192b40f0dc4ea362d7e5e565cccd32dfd72ddb7db638f3836d1712f2d3ea  src/app/frontend/content_refs.h
b9c544325c0eb1530d2d33fcf690ffa2f02df7cb1b917ffde29f2fd5da457f56  src/app/frontend/root_restore.c
af9fa941a714c0a8b5846a0c0b32ed0c2b42825b719e477242b88b9951ebd0c2  src/app/frontend/root_restore.h
7b1d986b38625548445ad41db0ecadc5b98147351c190d0b3753852a7a626549  src/app/frontend/restore_topology.h
b6f1177b44fca57fcc7841f5c4dc26a5971f41355cbbc456cc183d00176f132f  src/app/frontend/network_content_q3.c
6d568bb92e90f0200e74744f26c489854e2948b6f961ef040ed273891e4a78fb  src/app/frontend/network_content_q3.h
49f3fa6fb7a719618d1cec3b2aca307b42e902b8598fd4ce642325fbd4056087  src/app/application/publication.c
db5bd1880952c67ba7e8c09b1633b0e0d8d096bd26c91d92c6478a0ed873f50d  src/app/frontend/source.c
bb25afc458b7063e49893022f9dd947103606d621eb46ca0343a4183121a6436  src/app/frontend/source_restore.h
77c5cc11e830965c95560eaf14aeae86c2f308066cb58741b06e58ed75c31251  src/app/application/providers.c
142258403b52d38d625b7d54131e2cabb329f0da340e03f4204d455a634ea77d  src/app/frontend/remote_q3_initial.c current346
ec72222061973f5a43e1d31b7b48d0060ec49af6a03ec72a32a0415b571fd931  src/app/frontend/remote_q3_initial.h current60
ae7973d23112d467439f43b8cb861f8fc97e07c7c8b9cf3fbf19d679e4af14ad  src/app/application/save.c
63f59b46bf827f0ad2348f5a03a27d5aff9504a7ce0ae3463f3ce8eca66d396f  src/app/application/save_private.h
ed6891a11588762d845d0a5237618841e67ccd8733a9a88386fafc68fd6351b4  include/qa/persistence_application.h
419abd825bfe519cb8b5e4120c3fcf686e682d9037304f21bede704c1f231a79  src/app/frontend/persistence.c
1273b3c2298ad7caaa528c264a828b14a14663f667ffac812f4f98c4f2219169  src/app/frontend/persistence.h
73a876a8b614c164f3f750b5ce42f724eda619baa6c6177fee43a95e25e722bc  src/app/frontend/lifetime.c
fb6696cab3847044369343fd3690cf68b3a6e6c6cdf1db8214a97b9a1f01eb87  src/presentation/q3/models.c
463c93dfeafb266f949a8b6906a9e2c122eb7c79318779575a04c0ac79ed6cd2  src/presentation/q3/assets.c
31b9410b973ffbab6cced79cff350b0569c839dbcdae2fc5d836d8e0146a4dbe  src/presentation/q3/internal.h
47af9e448d9129ee626013d347055a2d47ec3a1fd3466ba7be60781a1076929f  include/qa/q3_presentation.h
03aa95b9d004ecd3a32067abb9245db9194d0987126fc675800442ec8c5c9ded  include/qa/q3_model_opening.h
```

## Whole-file donor coverage

Foundation helpers: whole `core/binary/index.ts`, `core/{common-error,common-parse,diagnostics,game-numeric,info-string,math,md4,numeric,omnitimer,q3-cd-key,q3-product-policy,qvm-math,resource-name-index}.ts`; actual `network/q3/pure.ts`; `contracts/{identity,numeric,math,scene}.ts`; and real `persistence/value.ts` and `core/commands/text.ts` callees. Source checks distinguish binary32/QVM arithmetic from Q1/Q2 donor binary64, source signed-byte token profiles, native versus game numeric parsing, source CRC-word order and seeded checksum feed, and mutable aliased vector operation order. This supplements the foundation reader and does not accept unreviewed B21 command/cvar/script bindings or B24 external executable behavior.

Archive: `content/archive/{types,index,pak,zip,source,loose}.ts`; mount/identity: `contracts/content.ts`, `content/mounts/{index,paths}.ts`. Maps: `formats/bsp-kind.ts`; every file in `formats/q1-map`, `formats/q2-map` and `formats/q3-map`, including entity/visibility helpers and structured extension readers.

Actual writable and saved-resource dependencies also received whole source reads: `compat/qvm/file-syscalls.ts`, `platform/files/{writable,contained}.ts`, `persistence/{recipe,shared}.ts`. The concrete PK3-only source zip flag and repeated non-zip relative seek are established by the real syscall donor; the apparent difference is refuted and is not a finding. Contained root publication/stream ownership and saved resource provenance, ordinal/digest/reference admission were traced through their actual lower helpers. The unrelated imported weapon/mod/native behavior persistence callees remain other lanes' scope.

```text
0973d51d024f2ac7f0a0f811f7e5f45da0a050e65ab2324b3ba4a6c94c648d92  donor/src/compat/qvm/file-syscalls.ts
b893a8926600172de2a00d0819bce10b8507084847f1b85b79307b0c922824d3  donor/src/platform/files/writable.ts
e5c8c5a83134a0dac910d7f56414dfe3bffa7842180babee8592d931897301b5  donor/src/platform/files/contained.ts
f672a150838efbffdad4a175eedffeebcbd7cb6bd8a8aab825e7779fc1932b51  donor/src/persistence/recipe.ts
e4cfa7c0abb2aa92a5b742f7d9860f8d14fc7c2746faaa446d4651db5742c22c  donor/src/persistence/shared.ts
```

Models: every file in `formats/q12-model` and `formats/q3-model`, including alias normals, animation, skin/animation metadata, quaternions, replacements and LODs. Images: every file in `formats/images`, including ordinary and Q3 profile variants, palette, WAD, mip readers, complete 1,129-line JPEG decoder and JPEG/PNG encoders. Whole render math helper `core/renderer-math.ts` was also read.

Common resource/model consumers: every file in `render/scene/models`, including the complete shadedot table; `render/scene/{resources,textures}.ts`; `app/bootstrap/{assets,model-loader}.ts`; `content/model-attachment.ts`, `contracts/model-attachment.ts`, and `world/session/resources.ts`. Donor alias authority, mount-rank replacement qualification, required Q1 indexed sidecars, Q2 scale diagnostics/source skins/source frame count, atomic replacement preparation, named/mesh attachment digest qualification, provider-specific palette/image identity and reverse release were traced. This donor coverage does not substitute for the moving native owner review.

Additional complete donor model registration/admission files were read: `app/bootstrap/q3-client/assets.ts` 145 lines, `content/q3/presentation/model-access.ts` 31 and `content/q3/foundation/assets.ts` 121. Their actual resource host maps registered models through the application model loader, retains selected model/provider identity, supplies bounds/tags, and reads genuine character mesh/skin/animation resources. The native counterpart's external callbacks and moving retained owner/cold joins remain separately unaccepted.

```text
1bc886ce4eeaa1c5bc4f7e32157fbcf5841da394770ed9cbbe47f27839d37a88  donor/src/app/bootstrap/q3-client/assets.ts
f9f18a33360d1bc38bf799cbc4deafc259cc6a6e6ecf8650d9ab07290fb1724c  donor/src/content/q3/presentation/model-access.ts
28d37ecf039260ad4132962dbcd181a339580f480f0782abbdda770a7f9f3d64  donor/src/content/q3/foundation/assets.ts
```

## Remaining audit work

- Join the current frontend/application shared pool, overlay, candidate restore and remote topology callers; FS6, WritableProvenance6 and VFSIssued2 acceptance/export hashes are reconciled, and local readers/catalog callers above are already read.
- Join other current readers for script/console/guest/platform source outside B01's foundation scope; do not broaden this report to a whole-project source acceptance.
- Continue reviewing the complete retained replacement/attachment/material/palette/provider descendants alongside implementation. F-B03-01's actual history repair, the installed Scene interpolation guard and registration receiver are source corroborated; amended FORMAT4's 1,449-line lower repair does not establish aggregate cold completion.
- Report concrete current source defects immediately and keep this existing document accurate. No new manifest, hash or freeze requirement is introduced. Missing judgments are not passes and do not hold implementation; no original B01–B05 goal is completed by this in-progress report.

## Verified ledger evidence boundary

Read-only `work_history` inspection covered all B01–B05 histories (each response reported `has_more: false`). Latest historical owner reports: B01 `r_ff115ad7c68c4786809d7f4ec57bb2ad`, B02 `r_561e04006f57460fa3fa269813f8b50b`, B03 `r_2ed03f49e9534980a322dc469a9b2e7b`, B04 `r_d74ff35d27f748a78ea5388cb62552b2`, and B05 `r_4b8fda3dcc6a495e9bc0d685b2301c28`. These entries have `kind: report`, `source: report:progress`; their presence in a `verifications` response does not turn them into a new independent judgment. They describe source contributions from 2026-09-26 and explicitly defer executable checks to P01. B03 higher-level catalog/download/application consumers and B05 common presentation/application consumers were separately delegated. Historical earlier execution claims preceded the no-execution correction and are not evidence for the expanded current source.

Own in-progress reports were actually recorded as `r_a92a93a5bf124fb7970695651a703943`, sequence 10794, on 2026-10-02T00:55:40.884Z, and `r_e67a398cfb73412ab4c918eb1b9d732c`, sequence 10795, on 2026-10-02T01:05:37.231Z. Its work remains in progress at revision 1. Both records are progress reports, not acceptance judgments. No original B01–B05 task status was mutated and no current done-report judgment was obtained.

Subsequent FORMAT4/caller progress is actually recorded as `r_a627f0a6a4d14634b2a2503f2cdd6b68`, sequence 10805, on 2026-10-02T01:23:51.219Z. It is a progress report, not a returned judgment. Its provisional caller line counts for `persistence_content.h` and `lifetime.c` were misstated as 46/371; current metadata verifies 43/367, reflected in the exact 4,221-line inventory above. This counter correction does not change the complete file reads. Its FORMAT4 acceptance was subsequently reopened for the new equal-frame MD3/MD4 witness, as described in F-B05-04.

The correction is retained as actual progress report `r_31491f4c30d64fa4a9815a85e922a33a`, sequence 10809, on 2026-10-02T01:28:19.160Z. The same update returned actual `vibecheck.stalled` verdict `v_4fd79d6617bf4b6a92ddeaa1f5d7d45e`, outcome `passed`, reason `stuck 0.22, abandoned 0.30`; `work_history` verified the verdict's `battery_id`, source and kind. That verdict checks progress/stalling and does not establish code, B01–B05, B34 or AUDIT acceptance. Work remains revision 1 in progress.

The amended FORMAT4 acceptance and additional provisional reads are retained as actual progress report `r_84b78bff71104046802cee9e347de4c8`, sequence 10814, on 2026-10-02T01:39:05.168Z. Its same-update verdict is `v_453cb68f64ce42c3b02e1293fe7c7597`, sequence 10815, battery `vibecheck.stalled`, outcome `passed`, reason `stuck 0.21, abandoned 0.29`. `work_history` verified both records; this remains progress/stalling evidence and does not accept the original criteria or aggregate source topology.

The additional complete 3,917-line persistence/lifetime caller read and current InitialUI roster exclusion are retained as actual progress report `r_2ff1ba47ebe24c17aa53a36a40775fe7`, sequence 10820, on 2026-10-02T01:54:16.023Z. Its same-update verdict `v_0eda27b8adcb4a6aabfe440d1a0fbb61`, sequence 10821, has battery `vibecheck.stalled`, outcome `passed`, reason `stuck 0.16, abandoned 0.17`. Filtered `work_history` metadata verifies these records and the work's revision 1/in-progress state. This is progress/stalling evidence; it does not establish original B01–B05 or aggregate source acceptance.

Current common owners, F-B03-01 and the installed registration/remote/InitialUI resource roster joins are retained as actual progress report `r_998ccb564fd6488cb990065bf1b72823`, sequence 10842, on 2026-10-02T02:34:13.189Z. Its same-update verdict `v_1262d3e5f6194816a16863acabb463b6`, sequence 10843, has battery `vibecheck.stalled`, outcome `passed`, reason `stuck 0.12, abandoned 0.18`. Filtered history verifies the report/verdict metadata and unchanged revision 1/in-progress contribution. The report explicitly preserves the user's no-gates correction and contains no original task completion or aggregate code judgment.

The actual QARS4/QAVF6 repair and complete 3,603-line reread are recorded as progress `r_5fd1d72804a743a0bee39efc302620ab`, sequence 10844, on 2026-10-02T02:38:45.642Z, correcting F-B03-01's previously open state. Same-update verdict `v_dbe3825c628b4d20937ca474e89d5b76`, sequence 10845, is `vibecheck.stalled`, outcome `passed`, reason `stuck 0.10, abandoned 0.14`. Filtered history verifies these metadata and revision 1/in-progress state. This records source progress and does not constitute an original B03/B05 or aggregate code judgment.

Fresh build definitions, the complete 7,286-line visual/media owner read and new concrete F-B05-05 witness are recorded as progress `r_aeb503058ab742f59bb53c2191579a4b`, sequence 10850, on 2026-10-02T02:50:45.693Z. Same-update verdict `v_fb2d30faf9994d0196c8209f548158d6`, sequence 10851, is `vibecheck.stalled`, outcome `passed`, reason `stuck 0.07, abandoned 0.14`. Filtered history verifies report/verdict metadata and revision 1/in-progress state. This remains progress evidence; the stalled-work check supplies no original-criterion or code acceptance and holds no implementation gate.

The fresh QARS5/exact-file and genuine Q1 sky owner/donor reads are recorded as progress `r_3ce9eeff9cd34c24b223992fb1db09ef`, sequence 10859, on 2026-10-02T02:58:49.851Z. Same-update verdict `v_73505d6e442a47d795bedefc037324d6`, sequence 10860, is `vibecheck.stalled`, outcome `passed`, reason `stuck 0.11, abandoned 0.19`. Filtered history verifies these metadata and revision 1/in-progress state. This is source progress/stalling evidence only; no code or original-task acceptance is inferred and no implementation prerequisite is imposed.

F-B05-05's actual material-only initial publication repair, the fresh RemoteQ2 owner reads and F-B03-02's scoped failure cleanup repair are recorded as progress `r_1abc22183c56437a8428d3a3fb768165`, sequence 10864, on 2026-10-02T03:09:27.355Z. Same-update verdict `v_e19581d605c54be0834b909454d2d19e`, sequence 10865, is `vibecheck.stalled`, outcome `passed`, reason `stuck 0.10, abandoned 0.19`. Filtered history verifies these metadata and revision 1/in-progress state. This records concrete source repair progress; no original-task or aggregate acceptance and no execution qualification are inferred.

F-B03-03's real acquisition-order repair and the whole RemoteQ2 Source/CLIENT-registry/metadata constructor reads are recorded as progress `r_25e1ef0baa7142ffbe4ada8486f8b3b7`, sequence 10869, on 2026-10-02T03:29:29.791Z. Same-update verdict `v_dfe05c79427f482d9b7eac50f6d074fb`, sequence 10870, is `vibecheck.stalled`, outcome `passed`, reason `stuck 0.10, abandoned 0.16`. Filtered history verifies the metadata and revision 1/in-progress state. These are bounded source observations alongside implementation, with no new review prerequisite, original-task acceptance or execution claim.

The actual Source exact-cache repair, QAVF7 header repair and fresh complete historical-opening, Visuals/RemoteQ2 initialization and Source upload/color reads are recorded as progress `r_7b84790dd1bd44e6a6ccb203e06e04b8`, sequence 10877, on 2026-10-02T03:41:36.487Z. Same-update verdict `v_e64ffc64f7fe40db986c25d028297a26`, sequence 10878, is `vibecheck.stalled`, outcome `passed`, reason `stuck 0.09, abandoned 0.13`. Filtered history verifies those metadata and revision 1/in-progress state. This is source progress and stalled-work evidence; it supplies no original-task, aggregate source or execution acceptance and places no review prerequisite before implementation.

The actual per-registration Source UI repair, whole QARS7 builtin continuation repair and author-confirmed fresh replacement rank witness are recorded as progress `r_a10284e00925405c9d50e0d406856acf`, sequence 10881, on 2026-10-02T03:52:35.598Z. Same-update verdict `v_ec662c9444e54becab8e15e7b0e5f200`, sequence 10882, is `vibecheck.stalled`, outcome `passed`, reason `stuck 0.09, abandoned 0.19`. Filtered history verifies those metadata and revision 1/in-progress state. F-B05-09's direct acquisition-snapshot repair remains underway; this progress record is not a code or original-task judgment and holds no implementation prerequisite.

The actual acquisition snapshot/remote continuation repair and resolved Q2 parsed-key witness are recorded as progress `r_d32ca98be74d4281bf00a4d01b490f94`, sequence 10885, on 2026-10-02T04:00:41.593Z. Same-update verdict `v_7f056d04bdc64852b8d9bb0a39f91b43`, sequence 10886, is `vibecheck.stalled`, outcome `passed`, reason `stuck 0.09, abandoned 0.16`. History verifies those report/verdict kinds, sources and metadata, with revision 1/in-progress state. The borrowed QC receipt join remains moving; neither this progress record nor the stalled check accepts code, completes original criteria or constrains production implementation.

NativeClient has ACKed FS6: `/tmp/qa-reviewed-20261001/files-fs6-LWhMnukM/manifest.md` records independent six-file/4,414-line source acceptance. All six submitted files were independently read whole here and their source hashes matched that export: `include/qa/filesystem.h`, `include/qa/q3_host_files.h`, `src/compat/q3_host/files.h`, `src/compat/q3_host/files.c`, `src/platform/filesystem_posix.c` and `src/platform/filesystem_windows.c`. NativeClient subsequently reports only the current host body/private pair changed by 26 lines for PortableWrite3, while the accepted platform/public producers remain unchanged. Their new current whole read is identified below; the immutable FS6 export remains evidence for its exact earlier snapshot. WritableProvenance6's 2,956-line accepted packet was exported at `/tmp/qa-reviewed-20261001/writable-provenance6-vXc43Vp4/manifest.md`; its complete manifest was read. VFSIssued2's additive getter changes only `include/qa/vfs.h` and `src/content/vfs.c`; these paths received a fresh whole frozen read, independent Common source acceptance and a source/hash-matched export at `/tmp/qa-reviewed-20261001/vfs-issued2-20261001/manifest.md`. Its complete manifest was read. The local retained pool and view codec were fully read before that addition.

PortableWrite3's observed `q3_host/files.c` 571, private header 38 and `checkpoint.c` 568, total 1,177, were freshly read whole here. The pure helper qualifies the actual stream's mapped current root, normalized path, source mode and genuine receipt while preserving separately admitted historical origin metadata. Portable incoming host validation decodes the complete writable/script/cvar continuation before ordinary restore can reopen a stream; actual restore retains mapped handles through candidate installation and closes failure-owned staging. No additional confirmed bounded writable admission defect was found. NativeClient now installs the real constructors/importers directly under the user's no-gates correction; no further export is planned. This supplementary read does not accept its unrelated game/script/cvar services or close B03's aggregate cold graph.

```text
5ef40b69bef5c58289d6b52f6aa591dabdae8d014d86932c2f709a3f5eccad03  src/compat/q3_host/files.c current571
ebe240682288d966edb38602324658709789b3e0e5f848504ca0675fbaa5ac25  src/compat/q3_host/files.h current38
d7f7ca52a98a0adc17577a5f8771f7dc84c77dbb95aa0e9bc92977ad30702dc3  src/compat/q3_host/checkpoint.c current568
```

NativeCGame's accepted ModelOpening4 export was additionally reconciled through its complete `/tmp/qa-reviewed-20261001/q3-model-opening/SCOPE.md`. All four current owners were independently read whole here and current hashes match their immutable copies: model opening header 27, models 327, private header 145 and `assets_save.c` 589, total 1,088 lines. Public `q3_assets_save.h` 91 was also read whole. The codec preserves the genuine first request, primary/LOD opening recipes and historical order/rank; issued-lineage checks do not reinterpret retired IDs as current membership. Source/resource/scene identity and unique adoption checks precede ownership transfer. No additional confirmed defect was found in this bounded producer/codec read. The independently accepted metadata packet explicitly excludes NativeView's moving policy consumers, the missing initial replacement constructor receiver and aggregate runtime/cold closure.

```text
f835abdadc510e16d780d60994684fd442198acf98758b11c0f0906515d7e43a  src/presentation/q3/assets_save.c
3d3d796e355ba40501e6bcdf3ce31dc0412638160e6306dd8c624a6080338f23  include/qa/q3_assets_save.h
```

## Source hash observation

Supplementary whole donor foundation helpers and actual callees are identified here:

```text
ddda74081ff18d01112513569d043ee3a801ce6cdd235665bd722d0fec3c2b45  donor/src/core/binary/index.ts
716807a72c0337a6ef948bc597b1e64314620a0ec414f65b6077f923dc1dee4f  donor/src/core/common-error.ts
dff8570a1ed6125da9de027dfa62065a05dce94d4522f6f2b04c94ea71583c65  donor/src/core/common-parse.ts
1df90e06ec85e02120ecdd89df52a4d934527ef5e5e2a78ab214077293d87a62  donor/src/core/diagnostics.ts
b339c7b37cae57a737ad4ee68b9f81fae9ee9e22bcc1590c013085fef29d055f  donor/src/core/game-numeric.ts
447fdee9f67f2cefa65992802cf4095f431340069ae2e1e1a27cb19d89d1d314  donor/src/core/info-string.ts
311482d27d42ea45eb295bad890b6706ab268ac8436871e214dd2bc2205ad0dc  donor/src/core/math.ts
7c504a8a7f531854008b472d89c6c5ba817f5cf2ee89d875ec39d829fe2b9b44  donor/src/core/md4.ts
7f27b5b593a5c14428707681a1e45202d6d3426e25e0ef2c411f63bf425c1760  donor/src/core/numeric.ts
e0ed182f8461d3d0af5c289841b90b99c1236a76387935873d87e8b4e48f8d39  donor/src/core/omnitimer.ts
2e9f059e8d228f60cd87015062d85e59fffdee247812059005b4a636b411b8f9  donor/src/core/q3-cd-key.ts
4904118c061a15a2f138debb76e7d361eb21e198a8bf3023f5d8030d7ea96885  donor/src/core/q3-product-policy.ts
61ff82845782c4bfce92043f4c5464086c7c64af8ac57e3140757dcc29a9bc0d  donor/src/core/qvm-math.ts
9cfa3e126eb967406c6a11cc6f17b1855e8063c1dcd1cdd72242614be31d2e00  donor/src/core/resource-name-index.ts
ce40084609efe71132de48dad264f34ef096787ec13138fce46d9784b46f41f1  donor/src/network/q3/pure.ts
04313420a8f6e557aaf53a4fa60fdff4197326293de97f6f67511d20997f768e  donor/src/contracts/identity.ts
443f346eaa8ddac024d85cfee5a832ba3efaaf6f49a397ba01e9d7d3cd3db5ce  donor/src/contracts/numeric.ts
d667cb98cbc0999d50ed45a791af1940b7c8bc33d7c10f874ada844f9a432d19  donor/src/contracts/math.ts
237f8b81527008e36c4a99b0829a5861b9596b8f7ce6ff81de65441e17294ba5  donor/src/contracts/scene.ts
10ba9ce9edd9bd61d79441c6e70c20d4d9c3adc74e2b9d52606cc613cb234fd4  donor/src/persistence/value.ts
d2bb459efa158eb44ce78b1a830747f0ff28ca51cd9803404bbb4c7f7629c005  donor/src/core/commands/text.ts
```

The following SHA-256 values identify the native foundation observation. This is metadata, not execution verification. `include/qa/vfs.h` and `src/content/vfs.c` identify the frozen VFSIssued2 versions independently read whole here and reconciled with Common acceptance/export. Four format-file entries below retain the pre-repair finding snapshot for provenance; the accepted current FORMAT4 hashes are recorded above. No source stability is inferred from an owner's silence.

```text
e84fc41c5434249432fa3b52f136942de22cd42da8d698415dd8de084bc06fbb  CMakeLists.txt
06742e1517741174a7f8eed156b8b41f314ed883f361d33ec789fab92260ea2f  cmake/Native.cmake
c4f15b5f360f87ed2490273e4a879ae5ab873164315df0453e23d90cd95931f0  cmake/NativeGuest.cmake
86b0bd3caec03c6f34cb98e137af027dddfae1fd2d446e71ea276430dc64bd79  cmake/NativeBundle.cmake
83e938ef967e90e4e18be98443920ef546e0096cc7bffe6d269f6bc9cfe0954c  src/main.c
f669234953a59ffe076ead3ab437f38abe7244257ed2ec25d6f8ef4be6d61cf6  include/qa/common.h
d2c8f08a614ccc42714bb047d8a4549c6c2070b27c732121383f4a3700d6e35c  include/qa/arena.h
99f490094bc5841ec3041883f2ef967f48330bac2ad74250738d04211dfbfbb0  include/qa/binary.h
227965e5a0f5040bcc941480154cbfd9f74e483a9629fdbf70e5fea2e3a44488  include/qa/hash.h
65a28cc85b01693c93f0d1605ea215460f47e710de8e776c6317ab2ac97cbdff  include/qa/strings.h
c8974ec33f7f7c8f827c4458b568be279374b0bd9f9b4348608e0755d358b799  include/qa/text.h
cc06a0ef82ad1211c519e6f49fc58bc7baeae581595d0bd9a382b7c764790a1b  include/qa/tokenizer.h
53378965f560bc2c24b03510b1bd8957c0452b5a7a2fde89e5dc356930a625e7  include/qa/json.h
286f2c5383ed3c4d16bee3eebfee4efd402e4724be9958860f2976ae762a2f6c  include/qa/json_writer.h
fbbdf6a86445bf7ac997ee39feb5a409cd16ac0d3c2c3514d0f0bafa1b6ac80e  include/qa/common_parse.h
296249be60d45b167a55891551cfef1eeacc095b0aedf1804e9dab6959a53966  include/qa/math.h
bcba1f022be001c179b8672429521fd8c67ad60141acd4d1f5dbcdbb840abefd  include/qa/source_number.h
6ecf367b4c52ad4f1f809ff4daf8be53ad1bd685ba6e3ca8566afd02b0d08e9e  include/qa/q3_key.h
21d682590a25b0158a60f2f8a3a2a70a177941b183ca00413992d5853b8138bc  src/core/arena.c
1159cb069d889d44fd0cd960af35ec141d5af8e887e0f5b7e2cbdbe9f2a30d79  src/core/binary.c
d66e5888a859f4c0999945925b7324e26d262ae8c1ec72f110a266d1311169a1  src/core/common.c
10563abf16c9578f7c9bf8b9333be9fd0f0abaa8fd0e34ef909d6517e3ec3125  src/core/common_parse.c
124e9591139a4a90063b98ca36a6f4534dd8c1f80667598de9ab488c48288e1b  src/core/hash.c
f28c219866137a7aa903007e7ece8224cac1c11e7f8ef7a7ea3d2fc69e4e9162  src/core/json.c
1bc2fb62d144c6b5e2277dd45e105c87630e4a86e8fc8cd7055e36527d8a98d5  src/core/json_writer.c
d47a86fc760f3206ad9f4fc83abfcc5a1c094b177bf942699730cfd3d7d660e6  src/core/normals.c
091725950c2ec44fe53adf917ad63939cb2f625d1d8c3ed1f6015ee59f09a119  src/core/number.c
29aeee2073d5515194f6be547fb9a6e52e51732b61d2c81bfc27765ad7593fda  src/core/number_js.c
8017182352bc48d964900b53e6192879400163d9dca68f15240e0b16983847d7  src/core/q3_key.c
1568001f98969fc9d59c11546e747729c6c02666b4eb77669247f3df7621def6  src/core/strings.c
b6e93e69a2495484233cce06f8f23b87376edd4a20a1967d29a991095e1f29a2  src/core/text.c
807c63008aa8022e452b822eb6d5a461e9854267ddf629231e6baa61247f0923  src/core/tokenizer.c
f401534db49eb344b29599d4bcaca597b7b0082efe84b9d6dace2ff8609d369a  include/qa/filesystem.h
750b62cec4d576f330d5c9b0161343862bd00ef7d48f7b31e5b3268b6a317a3c  include/qa/native_runtime.h
ba6abbabf21fc312ee272c080cdb0c85a784bbc31de58852fbb94df861576fa9  src/platform/file.c
4631d2a59d4c5dc45d06b9aea7dd4597ddf9f2d1bdf1876ea579c75a3ce1d054  src/platform/file_windows.c
48cc21eb4cc52b2ee2c80ccfdf9a4dbd204ae0d0c95d06ceb40440a127fb7d95  src/platform/mapping.c
14db7f51c9294c0a6e65647327c13cb14a938dd991e9e35eb4f8a74eb4071efb  src/platform/filesystem.c
9ceaf66edd675c500d2442d59581284a46edecf9f6fb8787cf7db6f9f4a5138f  src/platform/filesystem_internal.h
eec25defc998a5ea1864871b7977822dc9d5b511acdf07a9662cc07db91baf9f  src/platform/filesystem_posix.c
11eb356cadbbfb41067445d3138fbc29f57ec3fff0fbf7be59229394cdfb775a  src/platform/filesystem_windows.c
eed90294d986e9ec11060af5a5276ba23a7067ca3f6f1107c0cb3f8edafa0cb4  src/platform/native_runtime.c
3f1c0926e6dbc21306e848f408d65a61aa30d44e5598aaea1fbe271378744c2f  include/qa/archive.h
f56e172e4ec06fc4435f09bc410b8516a8a15d84585c18f76b95921dab6023a3  src/content/archive.c
05ad1c27027565aa5c8808284807edd642a9302b8cb0aa93f44921cf9a6fe623  include/qa/vfs.h
6a0841062ab305b7c7edb59b37fe6e8423ee7380c708a20eac65c6f04bd9b00c  include/qa/vfs_save.h
05e61a3b647ffdd85b367a1149fb2d6bb6359aabb34d8f0352c4a6e407823e1e  include/qa/vfs_view_save.h
2047946b3bd1eeed8fa9fa7be9e5c7023c6e97e5cb84dfef97f5cda274d6498c  src/content/vfs.c
2ca42bb48e4400c80e269a50bef38773b845e6a31bc29e53445b17aaabb7e525  src/content/vfs_private.h
3ebcc064fb17d93c5f2d6cb249ef29145bc952f9f441b698116c3652c1c60ec2  src/content/vfs_save_io.h
39ff7328574b8b41313527678e187568ea7cf72e74dc59855e7701193ccd2486  src/content/resource_pool_save.c
6feb46eddb7e3a023065ddc26d29b3c8a004e0ff70e53b47719e0bb19d4e795c  src/content/vfs_view_save.c
b0e3ad69221794052915a34230902ca5a11692cc1c8645210f409526438e720b  include/qa/catalog.h
b26ac1f02a1143d2fbf28755429ca7b99097207bf26e1ced4c72dc5f97e45cfd  include/qa/catalog_save.h
d95f0f90eafa27e82bedbda7db0ca653ef2bc565a151260fba9db1414e86c632  include/qa/catalog_write.h
9c84a98c10663a6d6c82465f2a4c2e14d2e679158d6a7773eeac839566dffe30  src/content/catalog/behaviors.c
5780ad7cef695ebaeef46eab2aef8716bb64fb2ef03b3f5fd2e56b235da59d29  src/content/catalog/catalog.c
57d1d017de3bea502ad4f28b2c88c6d9fa54520db75f3cc9f3e17dfefd566998  src/content/catalog/discovery.c
47365ce7c655c26dcf050c7536a671fd41fc7eb3e017565dcd66373537284a69  src/content/catalog/internal.h
e3069d0d92c8ea9970a8248c8de82dba6a7b333a50ad026798f41d369c2ef1c7  src/content/catalog/metadata.c
d0dfe3d87e7454b0a3d8a0354df2cb71dc4172c620db524dc83a25ae2875a4d6  src/content/catalog/mounts.c
e750d9e629cda826bd7d9303aa1cc54234fbf027f4bc70e66ba83db1036fac19  src/content/catalog/products.c
a0fc604be041a10b10406683a4f49e2474fb2153bff522495f8a12bf8d8dbf7c  src/content/catalog/q3_mods.c
9ac821be152a06e1c4b00bd67b815fae13e3afaf616b7fdef10719a0bdbb76bd  src/content/catalog/q3_product.c
e741b416581f3fb95a74bcad9482e1a62acbbe60842e5ab22c6844031fe5b466  src/content/catalog/q3_write.c
54645987f23a9474f8ce7bd0414dfdd9f86a92143ac94a4b690ec964cb74f56f  src/content/catalog/save.c
2bb3404c22a202c50f0bf6bd4a702b20117caca763f10b150030ba31a9fc6a16  include/qa/bsp.h
a5f5413b4d5ad4c926dcf7ab3cc92b56cadebcad891713eacadbbbd925c23b04  src/formats/bsp.c
120a6814f63462558e9a8e9e1ab10d78f85193f6cd0830542dc2bffbc1208f06  include/qa/model.h
eb677b73211325e41a3b3698500d917c8e8d8f35e63c8d8221492e9f3df21330  src/formats/model/alias.c
b269a366c040d2b9b555cf59f1021d2e1349cbe1435b7aae1ab441eb492da31b  src/formats/model/internal.h
77e7c5270a827e9ec7b4383610daa4cff75fe8b0b412738b1b970be4bc9544bd  src/formats/model/lod.c
7ca2fd05204ca4804032487b82961d386584a3ea043a4db3f84dcd1c1627219d  src/formats/model/md3.c
cab33ca8dd935b07b4666b53d4ef8f4ef095b4e5e4362b8034bd3891255e8178  src/formats/model/md4.c
bb024535c28310ed71738c941b301884c6779fece344459aeffce28629855c90  src/formats/model/md5.c
34a19f839413455df1d38be81f93d4b4b0190d40eb37784873fe6d298c7e5f39  src/formats/model/metadata.c
dec4cc1cf226266a947826c0c8de2f3b98f8f089ef3ab934f243ee6364cefe12  src/formats/model/model.c
ae23de3762b15948dfbed8c60f4094597f1c76c0f4f07a24184ff6afd3dd653c  src/formats/model/scales.c
b597fee1b67683f4c0c1f6143cea76eb41e4f537f96ffbcbc846e2ea90541b9c  src/formats/model/text.c
aa447e805aa4e6a89985d9a1240c5fb37d912b4b58e2e7df1c9d0d89b3089a4f  src/formats/model/transform.c
81617509f61ed2d54b7aa97fbe19e4fadefa322c56827224de1e163c73f03619  include/qa/image.h
10556358cf992793cf69ead3a6718cc9c07785866c20c516cf4f0753f70a432a  src/formats/image/gif.c
27388cf2ad65d85ff05c3bf04fb2cc8328e18b5a334b42742447e4fe915bb255  src/formats/image/image.c
71cf4b2423615a1e339c2ccc99f540d6832a236026ff11b46a2ca34189a570fa  src/formats/image/indexed.c
1826d5d3ad7d085a8935870b2f5d73c4bfc522cb2a7f3a4c1ac3db351b6f0d77  src/formats/image/internal.h
a7c4a3b4146468401c8bbdfa2c65c03f1b5bcd49217125e8f3df15fccaaf3f7e  src/formats/image/jpeg.c
9361eedec14afeedd8fd8097a500975b9ad90683b33e03c8b344233185b922f9  src/formats/image/png.c
cf39c137030e442e325c5046e1223d3cdaaa8b4076b5e820d4d67e124964e6a5  src/formats/image/raster.c
f987233e4acb191da72edfc220a289c0a9addf4daee92feb8e4972cc14313932  src/formats/image/wad.c
```
The new VFSIssued2 header (267 lines) and body (2,081 lines) were subsequently reread whole at the listed pair hashes. No confirmed getter defect was found in allocation-before-issuance, terminal uint64 counter, clone and retained mount paths. All four unchanged provenance paths exactly match `/tmp/qa-reviewed-20261001/writable-provenance6-vXc43Vp4/manifest.md`, which was read whole. Independent Common peer acceptance and the new pair's source/hash-matched export are reconciled. Whole aggregate source behavior remains under review while NativeView and frontend/application owners install the actual common and cold consumers; implementation has no freeze prerequisite.

Donor SHA-256 observation below uses `donor/` for `/home/buzzkill/Projects/quake-typescript/`. All listed files were read whole; this observation is not execution verification.

```text
f4155275cfa615c85cb9e7701261de0e8481d86e46a1fc50797f0974678c1532  donor/src/content/archive/index.ts
edf6db0a593b578e2548f1b9a16f7944908261b0e30bd77b903dc177646bd75c  donor/src/content/archive/loose.ts
cb5f4421d89ad960aa069407b8f8ee92aa59a3ba4bb5d70037413eadcb471f45  donor/src/content/archive/pak.ts
1cd1efd4cbc1a8ef456ec804be9716bd0b6ab0d36b8f4a23d92f2377906b2596  donor/src/content/archive/source.ts
b79a0e647ee56bfac084db46cd33886c5937c4b07b39f0ba39543d127f9ec58d  donor/src/content/archive/types.ts
ecf87e5627f726e8f521a7affc0b7a9360db32bde4990e3523d6b3811b364786  donor/src/content/archive/zip.ts
f1853f20bf5b5e99d5c63f0e65a0cbd4e5fab8572fffa845f55ea05ced92cfb3  donor/src/contracts/content.ts
db51eea590ed0daad2b90605d706f42c6aa7cd9ff9acec56c2d50a85432f41c1  donor/src/content/mounts/index.ts
254049c11f422dcfc9e3c30541a0e4804833706473aa601da5b89813fecaa375  donor/src/content/mounts/paths.ts
0a56068e98617b7459daf92fbd325e30f372a8aa06a682f562a633863299658f  donor/src/formats/bsp-kind.ts
abe2c5b70abf941f25745803d9515cd54280fbea43fecfb22b108b00d6f6722f  donor/src/formats/q1-map/entities.ts
48ea5d00c17ab0cc948861688bf3552449d4b3e6487e632e6877ac9b975cf59b  donor/src/formats/q1-map/extensions.ts
9eedc154d80d726565e3299084c5dbb36725ef9725cabe25834d69f733f8b7dd  donor/src/formats/q1-map/index.ts
472ba0bc98a97e2002b2e4627a8e186098606044d95f2eb485548193c9cfd7db  donor/src/formats/q1-map/queries.ts
a9465f0420190a2e2d1ea9b9f9855eba5b97d502438c5c5479684ee372159c96  donor/src/formats/q1-map/records.ts
877054a4d52905e7d353c615d6146793d35bcdd5e60f51bffb994a273cf1e36f  donor/src/formats/q1-map/textures.ts
ffb03a3160381bec6f9043dd2c5d49b819092129bddb1ad6e27fd99fe86dca8c  donor/src/formats/q1-map/types.ts
e0261d5e7585058a186a37d11ac487015f0a9386601dc88b7f0e9fcbbdcfc3ef  donor/src/formats/q2-map/bspx.ts
044d90da63db0fe28c32942383d80bd2ca9b2f58e7d93a719a985ada2794687f  donor/src/formats/q2-map/index.ts
1fce691cf6d7368ec9286a5d9aa9adb2db58509d04705d1c0e8831861e64dc28  donor/src/formats/q2-map/reader.ts
74a19588920edce1c346964bc03704d06d0bcc484d0a4a17dc34bde87a1f8698  donor/src/formats/q3-map/decode.ts
8daf33c48c78a053d7526fe69ebc1c54592ae8fafd7d0faaf7d12d87c30a5e6c  donor/src/formats/q3-map/entities.ts
f7cf00aa9aa15949ee64e36f1191591d8a9d40bae1e92ec5b1921126deb46b2c  donor/src/formats/q3-map/ibsp44.ts
b23efd1034f961968c383d3cd623ad882513233998eb2855c4276f80748e923c  donor/src/formats/q3-map/index.ts
e86da4666b01a48e6c86d85fbf56b12409af9fedd5a50c75a1ba2974c9dfbca1  donor/src/formats/q3-map/world.ts
6d83dbf7a728146fbb5f750e116f13493b07e70807181ec531b7987aef44cb1f  donor/src/formats/q12-model/animation.ts
0c82d1d1d58e8621ebb03ba0252732eea27895d988d0a1227347ec19ed121fbc  donor/src/formats/q12-model/common.ts
4a635d4ce8fadc0871487f94a46e21f0911375e66e25872cfeaa2a0413124b46  donor/src/formats/q12-model/index.ts
585f14f2a93954f01ac4dc5dacfcd36566f4173babd915579131f4b9c2eaa26c  donor/src/formats/q12-model/md2.ts
f9d712ce19526a25b59a99e818407fd0417ddf2964f745c1112c0df6ad8f5301  donor/src/formats/q12-model/mdl.ts
0d18f6b9b2e76a4602f113b82cefc1b0152cf35292d5506db0f6d7c637583d64  donor/src/formats/q12-model/normals.ts
2f9f6081678850357c7fffd1fda900596d628cc394af96e8cd76e511a038e787  donor/src/formats/q12-model/sprite.ts
53d4764591fcf1ed6f7ed49308395e4c2a00b6e275a51df169ca82a152934c96  donor/src/formats/q3-model/animation.ts
e71c74284d189d808549dfcf52606987d96f399dc7299f545c2453c647ba70b1  donor/src/formats/q3-model/index.ts
34ede2c00c65bac7b156899541f34032fa0ada5984a7e2acfcf4c6344856b29b  donor/src/formats/q3-model/lod.ts
6ef5119192fb6db60383f9cd4dacdda30c03208ccd089b667e286a22e8279a9a  donor/src/formats/q3-model/md3.ts
143bb667d01284308eeab427cbd2eec53657808ba80c630eba15d303e03e6fe8  donor/src/formats/q3-model/md4.ts
1dab77fba9680ecdcb2561545c99679ed0c5744676e79a47081f4e30d099c5bc  donor/src/formats/q3-model/md5.ts
c5b7213cf72d7d163efeea449b5bef1817ec56a32bd23c204d93fe4357a03b25  donor/src/formats/q3-model/quaternion.ts
e11411582c02ee0b3bfee7354bcffbb9ddbecca77524e2af9086f83540995831  donor/src/formats/q3-model/replacements.ts
3924bb0ff6ee1fbe9e147bbfa3e91f8eadfc308cfc22d6a82382befd4f7e06de  donor/src/formats/q3-model/scene.ts
d9d98261e596a6e16267c5afea2e39a5597b26f45780281a53b729e8052748e0  donor/src/formats/q3-model/text.ts
4cf9c190f184019b61c36bcc49d4a24ffeb2a838c6f1690d6c4db14cfb2d0478  donor/src/formats/images/IJG-LICENSE.md
ef8ca6943429ac18e16b6ec66289fa7769fb3091a8a9041bbcf5898decbe75af  donor/src/formats/images/bmp.ts
adbf8c2669377777a74e2994826f16ce67a5df08b33f3863bd9b37c75f5ea2f6  donor/src/formats/images/gif.ts
03d7370025752239fa6ceafe2b7440f4fb1529cab039bfdc51c933c9f77f6f6c  donor/src/formats/images/index.ts
03d64181dcb46ebfd1081ec4ec8864717afbd5ad7df2710a9b690168ed312add  donor/src/formats/images/indexed.ts
afc2ac61bab82af647c1f8e08ef66d7014106fb88ea10e5fbf1fc64cd6eaf6a9  donor/src/formats/images/jpeg-encoder.ts
113816d3583dd08277c35e85039055b82be0f55af16ce1269a1d62b922def698  donor/src/formats/images/jpeg.ts
88aa10a655021c0437a5181e4f794fbe614edfa0f6e98dfdde3981fd82484e2e  donor/src/formats/images/mip.ts
a0a330a148dee6648775dc2729a1b7b2f3a55a52568ee33204daa533bbef730c  donor/src/formats/images/palette.ts
e4f469cbe32ab103fcd4d64b6923ae563556b5431156fcc0ddfa87377520cc1b  donor/src/formats/images/png-encoder.ts
4a896b22145db188a55a98d47dcdb43ed12d052de3a022de31f876f472eea8e4  donor/src/formats/images/png.ts
e0b61db4bda0d288917dac825047f4449a2a10925bf0409352bca4dc80e39a38  donor/src/formats/images/q3-bmp.ts
f9c4223a9cd34ed27df55a59cd3b6eedf608326cd78979723da9ebfa5974cae8  donor/src/formats/images/q3-pcx.ts
d7cbdd3954238dc61b1ac6c204b10d0383d8da9c5cac7472b008bddb55789905  donor/src/formats/images/q3-tga.ts
5f79aa5d2a7cc9a23bc24df84b4b9f49a0a720bd00d4cda665710fb1bec53945  donor/src/formats/images/tga.ts
05a836ab6278b3568491d7f3820ee80180acf92f16548ec47a045aebafbd5a7e  donor/src/formats/images/wad.ts
501bbda001a146c03c2260c5c115e76ac25501d9015ff63e6b9b0c51d170fa50  donor/src/core/renderer-math.ts
ee16bee88c6e03bf37ecddd315a89d1a119e4e62510a6f6b0b2e2f1ff9d8b7cb  donor/src/render/scene/models/attachment.ts
f442c30ded0f6aa758ed3ccc1c82b5b826625abf0b78a89e6c94ae4f835890d4  donor/src/render/scene/models/grip.ts
ec4e94001c613198d59653cd0220c7c315d89f8ac4d495569d8c1336cde8ad17  donor/src/render/scene/models/image-path.ts
3763108609d00172f5b7e716db129cf88cbc04dce30145e01f06266d8f487313  donor/src/render/scene/models/index.ts
837a363ccfe5017799a40609dfc117fc39d0d19e427c8cf1e3fe1ab3e9c38b1a  donor/src/render/scene/models/light-sampler.ts
a01db9dfd035075ae38f3984687c9bfcb7ed228fc07a84db8e21f0471e7e82d9  donor/src/render/scene/models/lighting.ts
0797820b566df00b241ba79bd4b9f25ab9f28c6b6257f064382526499249c185  donor/src/render/scene/models/md3-bounds.ts
acb71bb3e9285280bd90f503d0ba84f4c3b9b53115555df3b59c36a22599180e  donor/src/render/scene/models/prepare.ts
2370ab29e6c0e3242feb74d59730131f255a2a1fa3c0a9d6edb7288c3144c1a7  donor/src/render/scene/models/renderer.ts
5bad9877a5a46530ad780dfcc9650d08ae4fb8655bb04633c01626f446d98955  donor/src/render/scene/models/replacements.ts
589cc5080272a36e3a1c36bfd9121b971e07b949676d4f5abb93f0613dac6294  donor/src/render/scene/models/shadedots.ts
a4d51f639f49233655d2ae09a1f003f56d08d6aa117dd86c2c00f8ee2829b781  donor/src/render/scene/models/shadow-bounds.ts
9a72d61edaab56ac3d8405ccfdc88a3b3d3571b6faf871981d09f6ad1639ef7d  donor/src/render/scene/models/sprites.ts
f960d0451cb9e9cce11ebcc9d031ff5f33551f17a63635c6e9139c1e63099914  donor/src/render/scene/models/transform.ts
d947b1e1ff830091271aceeebd13a600c7a89e84548b3e8918c02239c9528673  donor/src/render/scene/models/types.ts
8fd6978cd2f544e18f8897b75b7c50c98f3a9b7d368a92e79189e19b3fb76515  donor/src/render/scene/resources.ts
885b5d753f9a41c17d9272e0e0e76a254aba1f9a547fc273890ed87a3f29d179  donor/src/render/scene/textures.ts
f65953a0f60c7a3a5291fc08761e4d5bf5bec6041d91a395165c4885c0e6a07b  donor/src/app/bootstrap/assets.ts
045840ea3da3d0dc4382c20ff912a0b0d1d4c5f07998c7711cdc3c62c33b891d  donor/src/app/bootstrap/model-loader.ts
b201d77ef70c5e4c654b98912c418a1284a4f1e21c1dfd0579ae6cabc347b236  donor/src/content/model-attachment.ts
84a138477efbe7d6fa7cbeed49d3e77d177267d849e4b5ba21c7e91ec6a78b81  donor/src/contracts/model-attachment.ts
fecadd521dd91ea1f79bfd7d7e3081c8151df03e337641915cdad6e26a4f3ffb  donor/src/world/session/resources.ts
```
