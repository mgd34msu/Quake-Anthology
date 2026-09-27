# Rogue ending source integration

The native Rogue ending implements cooperative and camera branches, staged
actor movement/fire/teleport, camera tracking, time-machine reactions, cleanup
and finale text. It reuses Q1 missiles, teleport effects, target dispatch and
the existing campaign finale timer. Cutscene control is a typed application
service so the selected movement and character providers keep their own state.

An independent worker read the complete new `rogue_ending.c` and donor
`missionpacks/world/rogue-ending.ts`, all changed interfaces/callers, and the
donor finale timer and selected-player control paths. Review found that stage 5
could hide a live combat-provider read error and skip crashing the machine.
Camera tracking also needed generation checks after the tracked-body callback.
The author corrected both and the reviewer reread the changes. The final file
hash is `41c6c5330e90b7908e3d5f3749d3f08dbe27ab3bfa4c6cd2ad58cd503454c29d`.

The same review confirmed an application gap: its current cutscene consumer
rejects selected Q2/Q3 movement/character combinations. The application owner
is correcting that; acceptance is not narrowed to Q1. Buzzsaw path behavior,
earthquake/team/rune ordering before the ending and complete private saves also
remain required work. This bounded integration does not close B10 or B13.
No engine configuration, compilation or execution ran.
