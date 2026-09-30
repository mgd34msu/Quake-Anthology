# Quake Anthology

A native C rewrite of [Quake Anthology TS](https://github.com/mgd34msu/Quake-Anthology-TS), preserving its unified engine and full interoperability goal across Quake, QuakeWorld, Quake II, Quake III Arena, their rereleases, expansions, and mods.

One engine and shared world provide the combined capabilities of the games. Movement, characters, weapons, monsters, equipment, rules, and presentation can compose across sources, including independent selections for players and actors. Original configurations retain their behavior, and mixed campaigns retain their missions and progression.

The TypeScript project supplies the implementation and product design being ported. Original game sources provide fidelity references. Game assets are not included; players supply their own game data.

Implement Anthology's built-in gameplay as compiled C operating directly on the shared world. Remove machinery needed only by the TypeScript implementation. Existing mod formats receive the compatibility support they require, without routing ordinary gameplay through CPU emulation. C data structures, algorithms, and numeric handling should suit the native implementation while preserving functionality and required game behavior.

Share common engine code, decoded assets, GPU resources, and reusable working buffers across games and players. Write and integrate the complete baseline with sound ownership, direct native execution, and source review. Builds, tests, executable runs and sanitizers wait until all baseline source is complete. Then validate functionality, make deep performance improvements, and polish the finished engine.

- [Deep review and current evidence](docs/review.md)
- [C architecture and implementation plan](docs/plan.md)
- [Dependency graph and exact prerequisites](docs/dependency-graph.md)
- [Functional targets and source ownership](docs/source-map.json)

The C baseline is under development. Full integration and runtime qualification remain open. See the [dependency graph](docs/dependency-graph.md) for implementation and verification phases.
