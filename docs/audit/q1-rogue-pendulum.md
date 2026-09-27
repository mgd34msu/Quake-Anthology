# Rogue pendulum source integration

The native Rogue pendulum uses the shared Q1 map scheduler, canonical body,
damage, resource and event services. Its 26 phases derive from 13 immutable
bounds, with the donor's two reverse-swing exceptions and repeated frame that
does not relink. Damage, velocity changes, sound timing and world-owned impact
direction follow `src/content/q1/missionpacks/world/rogue-pendulum.ts` in the
TypeScript donor. Authored `currentammo` is a typed map field.

Root read the complete donor file and native `rogue_pendulum.c`, plus all changed
dispatch, field, runtime and sound-helper code. The existing sound helper now
accepts an interned resource and explicit volume so pendulums and Hipnotic
sound brushes share body-center placement. Hipnotic teleport sound uses the
source's voice channel. The implementation owner also read the donor and native
source before this independent review.

This is source integration only. Q1 private continuation capture/restore and
application wiring remain open; the pendulum's phase and shared impact direction
must be included in that continuation. No build, compiler, test, game execution,
generator or performance measurement was run.
