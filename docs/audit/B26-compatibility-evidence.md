# Source evidence for B26

Snapshot: HEAD 2a7d5a1397ea169c963b23593ba84c360a926ab2 with existing dirty source; plan 6, original graph docs/dependencies.json SHA256 915c4c50da8fcb60ed206f09f2d325eb7755f4c6f3f48b1c3d97d83d2eb98bae. This packet does not change the goal or criterion. Full assigned file coverage and finding history: docs/audit/compatibility.md. Source reads and targeted donor/reference comparisons only; no configure, compiler, test, parser, executable or benchmark run. Judge the implementation against the original source criterion, not whether this report honestly states incompleteness.

The read source contains substantive shared navigation: src/navigation/construct.c:504 qa_nav_graph_construct, AAS/NAV decoding and validation, spatial association, travel/route costs, movement prediction, runtime invalidation, train handling and checkpoints. Source comparison covered donor navigation contracts and suspected differences. Source monster behavior is separately implemented in native gameplay providers.

src/bots/chat contains resource parsing, response selection/construction, state and checkpoint support. src/bots/library supplies character, weapon/item, fuzzy-weight and genetic helpers; core.c:51 creates that resource library. perception.c and random.c provide visibility and deterministic RNG. All their bodies were read.

The complete src/bots inventory has no player-bot decision/action controller: no ordinary command producer that selects enemy/goals/weapons using the selected arsenal, handles team/objective orders and rerelease decisions, or persists that player's decision/lifecycle state. Support library APIs are not the missing Q3-derived player-bot core required by the task goal. The reviewed CMake target inventory also has no registered bot-support target. No evidence claims that reading AAS/route helpers makes a foreign-map bot playable.

Assessment: B26 remains incomplete because the central player-bot source owner is absent; no runtime check is needed to establish that absence.

