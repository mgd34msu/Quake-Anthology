# Classic QC QW reserved client lifecycle

The classic QW map producer now creates actual source actors for physical
slots 1 through 32 before executing map source code. They own their real
provider/source-slot records and QC body bindings. The original QC free prefix
is zero, so `nextent(world)` and source entity searches can observe disconnected
reserved rows. Dynamic allocations still start at slot 33. Declared QC
profiles and classic NQ keep their previous reservation policy.

Selected QC Character admission reuses that full actor through a pure accessor.
The genuine source definition remains `quakec:reserved-client`; player routing
uses the canonical roster and actual source client traits. A foreign Character
keeps its own canonical actor, execution provider and source metadata. Its QC
physical row becomes BORROWED after a qualified handoff, and the old placeholder
is released. Handoff retains private fields and applies the existing body
synchronization contract instead of clearing the row before source Prepare.

Ordinary canonical retirement replaces the physical source binding with a real
new QC-owned actor before forwarding the old full-generation release. This
preserves the raw reserved row while allowing canonical client retirement.
Session transitions, unavailable owners, source teardown and VM checkpoint
replacement suppress this allocation. Explicit QC `remove` of an OWNED source
actor follows the actual QC `canFree: true` contract: it unlinks, clears the
original freed fields and sets the FREE prefix, then releases the actor. That
actual FREE
row no longer matches ordinary retirement replenishment. Repeated removal of
a FREE row refreshes the source freed metadata. BORROWED actors retain their
existing canonical lifetime authority. The
`checkclient` consumer reads real QW reserved bindings, health, flags and PVS,
independently of connected client metadata.

The actual VM host now supplies the retained engine source clock to edict
allocation and free metadata. The game host forwards it through both initial
creation and level reset. Cooldown arithmetic keeps source seconds as binary64;
the original free-time field stores its final binary32 value. A think callback
may set guest global `time` to its deadline without changing this clock. Map
loading establishes the genuine one-second source start before source code.

QW Prepare requires an inactive connected full actor, actual retained spawn
parameters and canonical raw userinfo. It validates the source field types,
physical reference, stable name buffer and finite `sv_maxspeed` before clearing
the raw source words. It then writes physical colormap, team zero, the stable
32-byte name buffer and the optional gravity one and source maxspeed fields.
Only pending thinks owned by this QC source are cancelled. Begin requires that
prepared stage, restores actual parms and invokes the genuine spectator or
player admission callbacks. It retains QW team zero rather than applying the
classic NQ colors policy. A genuinely new deferred client runs `SetNewParms`;
continued map clients keep their actual captured source parameters.

The engine codec is version 7. It captures the prepared stage and qualifies
disconnected OWNED or connected OWNED/BORROWED physical bindings against the
actual full actor registry. The existing VM codec retains source bytes and
actor references; restore does not invent source actors or replay callbacks.
An explicitly removed reserved row cannot be admitted or captured as a valid
retained client pool. The donor's restore likewise rejects absent reserved
source actors; `checkclient` still observes the actual FREE row during play.
The pure connected/spawned membership helper can qualify imported CONTROL5
records before executable source admission is restored. Runtime command
admission separately requires the real attached initialized source. Raw QC
think and input use the actual source command and QW slice even when the
canonical Character or selected movement is foreign.

Source references read include the TypeScript `quakec-source.ts` constructor,
Prepare, admission and disconnect stages; `entity-host.ts`, `client-host.ts`,
`builtins.ts`, `source-slots.ts` and `q1-client-visibility.ts`. C source reads
include the actual session allocation/release domains, QC VM and private
checkpoint owner, map roster admission and network Prepare wrapper. The map
roster, CONTROL5 decoder and network observer/wrapper are separately owned
caller packets and require their own exact source acceptance.

Validation is limited to source reads, exact hashes and scoped whitespace.
No build, test, parser, game or other executable validation ran. Native QW
factory/admission policy, authentication cvars, after-EndFrame actions and the
local source intermission/view output producer remain separate requirements.
This unit does not claim complete native QW or runtime parity.
