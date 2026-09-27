# Shared bot navigation queries

The bot query owner borrows the existing navigation runtime and canonical
world. Graphs, route caches, source movement and entity observations remain
shared; the query owner retains scratch memory and result storage.

An independent worker read the public/private query files, all shared navigation
diffs, donor Q3 navigation and syscall adapters, and application prediction
drivers. The review found no confirmed defect in this bounded prerequisite.
It specifically checked route timing, goal limits, adjacency traversal order,
jump-pad admission, AAS prediction projection, and selected movement limits.
Root reviewed the public ownership contract, creation/binding/destruction and
CMake dependencies. Collision-reader and BSP-classification failures propagate
through checked callbacks; they cannot become a successful negative answer.

The review leaves an explicit application obligation: base geometry queries
may have no actor, but movement prediction requires a real admitted actor and
matching movement input. The donor application uses its first admitted player
as a fallback. Application composition must supply that behavior before using
unbound goal predictions. Full bot controllers, roster and application assembly
remain open under B26/B34. No engine code was compiled or executed.
