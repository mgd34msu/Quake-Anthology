# Application ownership integration, September 29, 2026

This is an incomplete B15/B22/B23/B26/B34 source packet. It does not establish
numeric completion, compilation, runtime behavior or baseline acceptance.

## Source changes

- A configuration metadata lease retains the existing instance descriptor,
  resource references, content view and identity independently of snapshot
  execution references. The final snapshot still invokes close once. A deferred
  provider close releases its metadata lease only after deconstruction succeeds.
  The close callback cannot reclaim its enclosing metadata owner while active.
- Provider construction retains the catalog that owns its selected product.
  Refreshing or retiring catalog snapshots cannot invalidate the product strings
  borrowed by retained guest service owners.
- An intrusive application list contains every prepared provider until successful
  close. Guest readiness checks include detached and retained UI/cgame instances,
  not only the current gameplay roster. Apply, frame advance, publication prepare
  and public destruction reject active guest execution. Configuration safety also
  checks native bot population readiness.
- Native Q1 providers retain a separate owner token from construction until
  deconstruction after callback retirement. Active operation and observer borrows
  remain separate so an idle retained provider can admit maps and checkpoints.
- Committed travel captures QC change parms and shuts down Q3 game/cgame map
  services before actor retirement or replacement of collision geometry. Native
  bots are destroyed before their old world/provider dependencies retire and are
  published only after the ordinary map/player admission path completes.
- Application options expose a typed, seat-specific Q3 client effect callback.
  Private guest system-info, map-restart, screenshot and disconnect consumers
  require actual outer owners; one seat cannot stop all independent seats.
- Q3 default services expose the real canonical world actor and a continuation
  velocity notification. The host writes source playerState and entity stores
  before notifying the existing shared control state, without recursive body
  writes. Admission and other missing server/frontend callbacks remain open.
- QC declarations qualify immediately after loading the program, before provider
  registration. Failure releases typed qualification, program and metadata; final
  close releases qualification before program storage.
- Explicit CMake source lists include newly written C translation units. This is
  registration metadata and is not evidence that the targets compile or link.

## Evidence and remaining scope

The immutable metadata lease and guest host/client packets have separate bounded
source reports in `docs/audit`. The changing root consumer packet is being frozen
for independent review. Manual call-path inspection and whitespace checks are
source checks only. No compilation or runtime result is claimed here.

Full qualified secondary guest roles, canonical guest combat/inventory, real
server admission and bot roster callbacks, frontend snapshots/command effects,
ordinary actor text-command ingress, and complete save/network workflows remain
baseline obligations. This packet does not close B25 or B34.
