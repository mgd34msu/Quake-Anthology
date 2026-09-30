# Native Q2 frontend service source unit

The tools owner implemented src/app/frontend/native_q2.c against the actual
native-host profile/import contracts and the existing frontend owners. This
unit uses source reads, diffs and source hashes only. No configure, compiler,
parser, test, program, game, benchmark or sanitizer ran.

The guest engine gives the frontend a lease before native instance creation.
The lease clones its actual provider content view and borrows the private
cvars through guest shutdown and host teardown. Guest instance allocations
hold eight checked reusable string return buffers. Host teardown frees those
allocations; frontend release does not access the retired instance. The lease
releases pictures, classic font, localization catalogs, image and audio owners,
world text and its content view after source shutdown. Source sound retirement
stops its actual mixer owner voices.

Original/rerelease source pictures use the shared scene image owner. Classic
characters load the source pics/conchars.pcx. Font layout/drawing uses the
existing font selection and clipping owner. Client text input and key binding
queries read the actual bound seat console/input owners. Localization reads
the actual source catalog and seat language policy. Clipboard uses SDL. Native
debug imports decode checked guest vectors/colors into the same tools debug
store; world text uses the existing font world store and shared scene view.

The typed hud_view callback returns frontend_viewport and the same safe-area
and scale policy as the ordinary HUD. Draw imports require the actual cgame
host admission seat, including source Init callbacks. Frame preparation does
not infer a seat for game providers. Sound resolves the guest engine's actual
registered sound name and routes through the shared audio mixer with full actor
identity mapping and source ownership.

Debug output uses the root ordinary console. Ordinary and center prints use
the actual platform print callback only when the console is not redirected,
and remote wire output remains guest-owned. Recipient matching uses the full
actor ID; local presentation does not interpret the opaque protocol queue.
The root and frontend retain tools/debug/audio/render owners through source
shutdown, then retire network/tools before the application console.

Canonical cgame frame/client/server observations, ammo warnings, autodemo,
game localization print and bot/navigation imports still require their actual
guest application producers. Missing bindings report unsupported; this unit
does not invent frame validity, client names or completion. Companion role
admission, exact DrawHUD invocation and gameplay authority remain guest-owned.
The source unit and corresponding consumer hooks need independent frozen
source inspection. This is not B33, native Q2 parity or baseline completion.
