# Renderer controls implementation

These production paths are implemented in source. No compiler, executable,
parser, runtime or performance verification has run in this lane.
Implementation continues alongside source review; review does not block work.
Root owns CMake and Git integration.

## Retained renderer controls

`include/qa/render_controls.h` and `src/render/controls{,_private,_save}`
define settings embedded in the actual CPU or GL renderer. A retained ticket
prepares primitive and compiled-array values, checks the real renderer,
publishes the scalar transfer, and closes through abort or finish. Destruction
waits for the actual ticket. A surface child can coexist with it; publication
consumes the primitive child before publishing the surface.

`shared_render_controls.c` qualifies the returned ENGINE edit and the physical
`r_primitives`, `r_allowExtensions` and `r_ext_compiled_vertex_array` rows.
Automatic mode combines the selected extension preference with real native
extension and procedure capabilities. The registrar supplies actual rows.
The live helper reads committed physical rows using the registry's case rules
and updates the retained primitive mode when its integer changes. It runs
before execution in ordinary presentation, Source screen update, native and
remote loading, remote module screen update, and campaign/System cinematics.

Mode zero selects indexed elements when compiled arrays are available and
array strips otherwise. One uses array strips, two indexed triangles, three
discrete strips, and other integers suppress controlled Source stage draws.
CPU has no compiled-array extension. GL uses its actual array-element and
compiled-array procedures. Genuine paired Source discrete draws retain their
bounded rejection because the donor emitter passes invalid multitexture
targets while TMU1 is selected. Ordinary paired direct and other primitive
paths remain available.

Actual Source stage producers set `source_primitives`. Direct outer sky,
beam, stencil volume and finish draws retain their independent paths. CPU
schema seventeen and GL schema seventeen retain the physical controls, scratch,
scene bank, shader/world/image holds and reached pipeline state. Frame schema
eleven retains Source provenance, policy, direct draws, output domains and the
actual allocated vertex extent. Earlier admitted schemas decode their own
layouts; frame schema two has no primitive-provenance byte.

## Scene policy and clocks

Reached CGAME paths read actual CLIENTCG `cg_shadows` after scene/effect
preparation and current-owner checks. UI and genuine construction before CG
Init retain their absent-row fallback. Native construction proves actual
uninitialized service state before accepting an absent row; detached restore
precedes registry import and retains its saved fallback. Source, native,
remote runtime and remote module callers consume live scene policy.

Model shadow admission uses actual fog index zero and the selected original
material's opaque sort before remapping. Mode two excludes personal models
outside portals and `RF_NOSHADOW`/`RF_DEPTHHACK`. Mode three permits projection
shadows with `RF_SHADOW_PLANE`. Source stencil diagnostics read physical CPU
or GL capability. The existing cross-format shadow union remains.

Source clocks convert integer milliseconds to binary32 and multiply by
`0.001f`. Entity and target-shader timestamp subtraction also use binary32.
Material, shadow and sort consumers resolve one remap link. Actual Source
target timestamps and current-record bindings are distinct from shared
per-binding offsets and future registration recipes. The material library's
schema retains those distinctions and its genuine Source profile tag.

`q3_render_policy.c` projects actual canonical rows into Source material
profiles, model/curve LOD, rail dimensions, world/entity visibility, portal,
sky, dynamic-light, light-scale, near-clip and diagnostic consumers. Fresh
constructors use the real pending ENGINE edit where present and initialize
Source near-clip bounds once. Detached import keeps saved construction policy.
Prepared material policy rebuilds genuinely tagged Source libraries.
Live UI registration and color/upload policy remain coordinated with their
actual material and color owners.

## Visibility and frame behavior

Source leaf marks, PVS generation, view cluster, scene area mask and retained
surface backend light masks belong to the real world owner and its schema
three continuation. Reached `r_lockpvs`, `r_novis`, area-mask and cluster
modification rules select actual marks. Cluster reporting acknowledges the
real physical canonical row and uses the actual Source print callback.

Prepared per-view visibility captures admitted surfaces and incoming light
masks in frame-owned storage. The parent retains its prepared list across a
portal's live PVS changes. Actual visible leaf bounds determine the Source
far plane and sky scale; no-world scenes use 2048. A world-disabled primary
uses fresh zero bounds, while a child inherits genuine parent bounds.
Generic configured far planes keep their existing contract.

Source packed sort uses admitted `source_dlighted` separately from retained
backend light masks. Zero incoming lights preserve prior backend masks;
nonzero incoming masks update actual surface cells. Sorted Source batches
union backend masks. Source scratch, sorted empty/sky surfaces, partial
writers, batch limits and diagnostic End consumers belong to CommonSource
and remain under active joined review.

Genuine Source view/picture/issuer paths mark frame backend policy. Actual
`r_skipBackEnd` is read before Source End and immediately before execution;
pending work retires without stage writes, and execution/presentation guards
skip backend work and swaps. `r_clear` clears color/depth to donor magenta at
draw-buffer execution. Frame reset clears policy flags. Cinematic formats
do not manufacture Source provenance.

The additive Source body endpoint uses the actual busy primary model owner
and component material owner, primary geometry and component material fields,
and the reached Source clock. Captured brush body passes produce no geometry,
matching the donor path. Existing selected-body submission stays separate.

The component endpoints use the completed component packet's own assets,
time, entity/poly references and polygon fog under its retained body lease.
They add surfaces to the actual primary view without replaying guest Draw or
inserting another world/view pass. Inline Source entity lighting reads the
same actual transformed model bounds and four-plane/no-cull admission used
by the world submission.

## Reached pipeline and image admission

Source issue streams execute each reached prefix on the physical CPU/GL
renderer. Cull, polygon offset, state bits, client-array selection, coordinate
pointer domain and each texture binding publish at their actual iterator
boundaries, including before a later stage or video callback fails. Direct
beam, axis, outer sky and stencil paths preserve the fields their donor does
not write. Source stages preserve reached depth range, offset, stencil,
color mask and line width. Internal GL composite passes restore native state
around output resolve; recovery restores the saved pipeline and bindings.

Source vertex storage distinguishes the active tess count from the allocated
1000 retained cells. CPU transforms referenced inactive cells; GL uploads the
actual storage while keeping the active compiled-array lock count. Generic
mesh indices retain their ordinary active-count bounds. Frame recovery saves
the full Source storage instead of filling inactive cells with new values.

Actual successful present/swap flushes pending Source UI before rollover.
The stable frontend callback reads the current physical `r_skipBackEnd` into
that reached frame; skip preserves pending cells and avoids native Swap.
The renderer retains actual Source frame scope through generic overlay chunks,
so generic-only presentation does not consume Source skip policy. A reached
Source `GL_FRONT` frame completes its logical End without a native buffer swap.
Successful rollover advances the real scene bank and resets only frame
membership and 2D projection state.

`r_textureMode` initializes before Source images and applies modified values
at genuine frame begin. Source image admission registers immediately,
records constructor order, retains the actual bank and distinguishes a
cached requested image from raw object zero after upload. Texture-mode
traversal uses that registry and the retained texture unit/cache. Recovery
saves both cache state and actual-empty state and rebuilds registered order.
CPU admission retains real image storage and bank ownership in the same
creation-order registry. Both backends enforce the authentic 2048-image
limit. CPU texture-mode traversal updates the actual bound image's sampler;
a cached raw-zero binding preserves the registered image's prior filter.

Actual object storage retains each uploaded mip level separately from its
requested image. A reached `r_nobind` upload can replace dynamic-light or
default-object levels while preserving higher levels that were not written.
Sampler state and internal base format follow the actual object. Source draw
resolution reads that storage; incomplete units do not participate in the
CPU or GL combiner. Recovery includes object zero's native pixels and sampler,
the named objects' actual storage, and the immutable image and bank holds.
Pure metadata observers expose those image roots without native callbacks.

Cinematic uploads bind the genuine registered scratch slot even for a clean
frame. A size change writes RGB8 level zero and selects linear/clamp sampling;
a dirty subimage preserves the selected object's prior format and sampler.
These reached uploads do not repeat constructor raw-unbind behavior.

Prepared resource rebuilds expose only genuinely new completed Source
uploads, with their original constructor sequence and texture unit. The
renderer image child simulates no-bind selection in actual constructor order.
It retains separate CPU storage or prepares replacement native GL objects.
Preparing object zero holds the renderer image lease and retains the installed
storage for checked rollback; capture and execution exclude that lease.
Abort restores object zero and retires candidate objects. Publication transfers
them after resource-bank and surface transfer. A genuine Source renderer
restart replaces its registry and counts the rebuilt roster against 2048;
ordinary resource updates append only completed new registrations.
The final compositor publishes native image admission after its actual
surface/gamma transfer and disposes the child before the bank tickets.

Remaining joined work includes complete caller cleanup and validation of the
combined current/recovery paths.
CommonSource owns End/scratch and scene-bank producers; Source color and
legacy Q1/Q2 behavior have separate active owners. This document does not
claim executable or performance verification.
