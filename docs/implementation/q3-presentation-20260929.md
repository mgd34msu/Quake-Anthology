# Q3 presentation implementation, 2026-09-29

This packet implements production scene output for the existing seat-owned Q3 presentation service. It does not establish whole-project baseline completion. Source checks only were permitted; no compiler, parser, build, executable, test, gameplay probe or benchmark was run.

## Native output

`src/presentation/q3/render.c` implements `qa_q3_presentation_render`. It converts a source refdef into the shared view, inverts Q3 exclusion-area bits into the shared visible-area mask, retains source time and text rows, and submits world geometry, admitted polygons, models and effects through the existing renderer. Each view finishes once through the shared material order. A source portal search produces at most one child before its parent, with independent PVS origin, clip plane and mirror basis.

Model submissions preserve source entity order, frame wrapping/fallback, old pose, skin/custom skin/custom shader, shader time and entityTranslate coordinates, explicit axes, source flags, depth hacks, first/third-person visibility, light-grid fallback/dynamic lighting, selected-frame MD3 fog origin/radius, LOD and source shadows. Inline/BSP models use the shared world model service and its new optional entity material descriptor. Beam/default-model paths retain the caller's render state; sprite, rail and lightning geometry uses shared helpers. Missing selected LOD storage reports a format error rather than dereferencing NULL.

`src/presentation/q3/picture.c` implements shader pictures and remaps. Pixel-space clipping and shared material deformation run before orthographic projection; emitted picture stages override depth/cull for 2D. Per-seat draw color remains private. Picture shader stages use global render milliseconds, cinematic callbacks and shared registration identities. Renderer-wide remaps use the application callback; standalone owners fall back to the selected material library and world.

`qa_q3_register_picture_image` registers a shared font atlas image through the material service and the existing Q3 shader handle table. It allocates no duplicate atlas pixels. The existing host font export can bind its image callback to this helper.

## Donor evidence read

- `src/content/q3/presentation/scene.ts`: independent scene admission, polygon fog at admission, refdef view translation, resource identity, source model descriptors.
- `src/content/q3/presentation/ref-entity.ts`, `refdef.ts`: flags, procedural entity kinds and source record fields.
- `src/app/bootstrap/q3-client/scene.ts`: polygon-first admission, source entity ranges, default models, procedural shaders, brush models, first/third-person visibility and shader time/UV propagation.
- `src/app/bootstrap/q3-client/view.ts`: split-screen weapon projection and translated first-person references.
- `src/render/commands/material2d.ts`, `src/text/draw2d.ts`: picture clipping, pixel-space shader deformation, seat translation, normalized draw color and 2D state overrides.
- `src/render/scene/portal.ts`, `src/render/scene/world.ts:prepareViews`: first qualifying visible world portal, source rotation/oscillation and one child before parent. The donor scans world surfaces; an earlier worker message claiming model portal discovery was explicitly withdrawn after reading this implementation.
- `src/materials/q3-lighting.ts:setupEntityLighting`, `src/render/scene/models/light-sampler.ts`: normalized native adaptation of source light-grid fallback, ambient minimum/clamp, lighting-origin selection, dynamic direction accumulation and foreign-map irradiance.
- `src/render/scene/models/prepare.ts`, `renderer.ts: fogFor`: source frame repair, bounds-based LOD radius, selected-frame MD3 fog sphere and MD4 source-order fog exclusion.
- `src/render/scene/particles/primitives.ts`: rail widths, source unwritten vertex fields, beam state retention and default model output.
- `src/compat/qvm/client-render-syscalls.ts`, `src/app/bootstrap/q3-client/qvm-scalars.ts`: actual QVM render consumers and byte-unit light-for-point boundary.

These files were inspected as references. No donor or original engine file was copied into this repository.

## Application interfaces and real consumers

`src/compat/q3_host/presentation.c` is the production UI/cgame trap consumer added by the root worker. It now reaches the render/picture/remap definitions, existing resource/audio/cinematic functions and shared font service.

The application must supply these services when constructing a seat:

1. Bind one shared `qa_material_order` to the frame before source groups. Provider material libraries share that order.
2. `prepare_view(context, refdef, scene_options, error)` receives source-derived defaults and can project selected-map styles/fog, translated camera, map family, source first entity, weapon offset, split-screen status and supplemental weapon suppression. `scene_options.state` starts with shared default state and can retain the source render state for beam/default-model helpers.
3. `submit_view(context, scene_options, frame, error)` can append native supplemental content into the same child or parent view before shared sorting finishes. It must use the same frame/material order and honor `world.view`.
4. Set the renderer-wide `remap` callback to reach independently selected provider libraries. The standalone fallback cannot compose cross-library replacements.
5. Configure `qa_material_library_set_video_start` before provider registrations; prepare the shared `qa_material_movies` owner once globally at frame start; bind `video_frame` and `video_context` to its resolver. Every reached world/model/effect/picture shader forwards the same resolver.
6. Bind the host font-image callback to `qa_q3_register_picture_image`. Font library lifetime remains owned by the application.

Private scene arrays, draw color, entity parser cursor and 16 cinematic handles belong to each presentation seat. Decoded model, shader, image, sound, font and movie assets remain shared. All external presentation callbacks run while the presentation owner holds its existing busy lease, so public teardown and mutators reject synchronous reentry.

## Source review and current limits

The worker checked public definitions against the header and their actual host calls, model/scene/effect contracts, source material sorting, picture geometry/deformation order, frame rollback and callback lifetime. A failed render rewinds only this render's command/group publication; retained image/geometry pins and frame arena allocations remain until frame reset, matching the shared renderer's documented ownership rule. Previous completed views are preserved.

Independent root-owned renderer changes reviewed: Q3 lighting-origin flag 128 no longer selects non-normalized axis handling; model fog context now supplies the volume RGB; inline entity shader time adds to surface time and explicit material fields transfer synchronously. No concrete defect found in those bounded changes.

Worker reported two confirmed host boundary corrections to root: `qa_bounds` fields are `mins/maxs`, and scene light samples must convert normalized ambient/directed channels to byte units when writing Q3 guest records. Root owns those fixes.

The shared foreign-model shading path currently needs separate provider appearance semantics from Q3 guest command flags. This worker reported the exact native/donor discrepancy; root owns its correction. Application construction and material-movie/frame bindings are not claimed complete by this packet. Root is implementing them separately. Optional authored flare admission remains owned by the application/shared world callback rather than duplicated here.

Q3 projected-light masks use at most the first 32 admitted lights, matching donor application submission; the retained admission list is not truncated. Render rejects a source entity range that would collide with reserved world entity 1022. Source shaders require the shared material-order owner at frame finish.

Whitespace checks (`git diff --check` for owned source/header) passed. No runtime or performance result is claimed. Independent peer review of this frozen implementation is requested from root.

Picture follow-up source review: source material deformation keeps the authored camera basis while its final projection uses pixel coordinates with clip Z fixed to zero. The native picture path now retains the last successful material view, optionally accepts `prepare_picture` from the frontend for pictures before the first world view, and overrides only the emitted MVP/depth/cull/clip behavior. This avoids substituting an artificial camera for autosprite/environment mapping. The freeze hashes were refreshed after this correction.
