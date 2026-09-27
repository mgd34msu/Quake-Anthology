# Shared Q3 scene admission

Polygon fog selection now queries the existing scene world's fog records by
inclusive bounds overlap, preserving first-match source order. Sphere queries
keep their strict boundary behavior. Both project the same fog record; no
presentation-side fog list is added. Invalid bounds and misses clear output.

The existing sprite geometry routine is shared separately from draw submission,
so Q3 presentation can submit authored material stages using the same geometry
as particles. Particle submission delegates to it with unchanged draw setup;
the mesh remains owned by the frame until reset.

Root read both complete bounded diffs and the donor polygon/procedural fog and
sprite routines. The primitive geometry body is unchanged by extraction. The
presentation consumer remains under implementation. No engine execution ran.
