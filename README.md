# Quake Anthology

A C rewrite of [`../quake-typescript`](../quake-typescript), preserving its unified engine and full interoperability goal across Quake, QuakeWorld, Quake II, Quake III Arena, their rereleases, expansions, and mods.

One engine and shared world provide the combined capabilities of the games. Movement, characters, weapons, monsters, equipment, rules, and presentation can compose across sources, including independent selections for players and actors. Original configurations retain their behavior, and mixed campaigns retain their missions and progression.

The TypeScript project supplies the implementation and product design being ported. `../qsrc` supplies original-source references, and `../qfiles` supplies game assets for development and verification. Game data stays external.

Implement Anthology's built-in gameplay as compiled C operating directly on the shared world. Remove machinery needed only by the TypeScript implementation. Existing mod formats receive the compatibility support they require, without routing ordinary gameplay through CPU emulation. C data structures, algorithms, and numeric handling should suit the native implementation while preserving functionality and required game behavior.

Share common engine code, decoded assets, GPU resources, and reusable working buffers across games and players. Build the complete baseline with sound ownership, direct native execution, and proportionate correctness checks. After baseline completion, evaluate gameplay, make deep performance improvements, and polish the finished engine.

- [Deep review and current evidence](docs/review.md)
- [C architecture and implementation plan](docs/plan.md)
- [Dependency graph and exact prerequisites](docs/dependency-graph.md)
- [Functional targets and source ownership](docs/source-map.json)

Current stage: baseline C implementation. The [local graph](docs/dependencies.json) records the plan published to vibecheck-jev; the ledger tracks claimed work and verified status. Check graph consistency with `python3 tools/check_plan.py`. Development uses local Git commits; the project owner will add a remote later.
