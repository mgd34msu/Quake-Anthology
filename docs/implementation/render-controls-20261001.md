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
beam, stencil volume and finish draws retain their independent paths. CPU/GL
schema four retains controls and Source scratch; frame schema three retains
primitive provenance and frame policy. Legacy frame schema two imports with
the new frame flags false.

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

Remaining joined work includes actual Source color/hardware-gamma/upload
authority, CommonSource End/scratch consumers, complete caller cleanup, and
continuation/source review of the final combined bodies. Legacy Q1/Q2
renderer behavior and material video producers have separate active owners.
