# Hipnotic authored spawn templates

func_spawn and func_spawn_small use the existing native spawn and clone paths.
Root read the complete donor hipnotic-spawn.ts and the new C implementation,
plus relevant entity-services clone/spawn and charm behavior. Random spawning
retains all five dormant templates in source order and adjusts monster counts
after initialization. Explicit templates preserve the source spawnfunction and
spawnclassname distinction. Typed state retains model, solidity, bounds, think
continuation and the selected master actor.

Template construction supplies the mold's full body before source spawning and
temporarily selects deathmatch zero for synchronous nested native callbacks.
Activation preserves the source distinctions between spawnmulti equal to one,
nonzero and zero, along with fog, horn charm and counter/removal order. Horn
references use same-registry actor history rather than requiring a still-live
charmer after a callback.

Root review found that the existing clone path allocated its canonical actor
with an empty definition before copying the private classname. The donor creates
the clone with the source classname. The worker corrected this, and root reread
the fix: shared/foreign observations now receive the same definition as native
state. Clone copying also reacquires generations after callbacks and cleans up
by captured actor ID.

CMake includes the new source. Full Q1 continuation serialization and application
authored-field consumers remain open. This bounded source review is not complete
Q1 family acceptance; no engine build or execution was performed.
