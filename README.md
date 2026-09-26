# Quake Anthology

A C rewrite of [`../quake-typescript`](../quake-typescript), preserving its unified engine and full interoperability goal across Quake, QuakeWorld, Quake II, Quake III Arena, their rereleases, expansions, and mods.

One engine and shared world provide the combined capabilities of the games. Movement, characters, weapons, monsters, equipment, rules, and presentation can compose across sources, including independent selections for players and actors. Original configurations retain their behavior, and mixed campaigns retain their missions and progression.

The TypeScript project supplies the implementation and product design being ported. `../qsrc` supplies original-source references, and `../qfiles` supplies game assets for development and verification. Game data stays external.

C data structures, algorithms, execution backends, and numeric handling may improve on the TypeScript implementation. Optimizations must preserve functionality and required game behavior.

- [Deep review and current evidence](docs/review.md)
- [C architecture and implementation plan](docs/plan.md)

Current stage: review and planning. Engine implementation has not started. Development uses local Git commits; a remote will be added by the project owner later.
