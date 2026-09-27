# Native Q2 Medic source integration

The Medic implementation covers classic, Rogue and rerelease corpse selection,
cable checks, retry/abort, revival, attack and pain. Revival reuses native monster
admission and retains the retired sidecar until its active continuation returns.
Rerelease maximum/base health, armor fuel, reinforcement provenance and summon
counters pass through that admission. Checkpoint version 6 adds the associated
healer, bad-medic, maximum-health and armor fields.

An independent reviewer read the complete new Medic files and every changed
header, core, AI, action, combat, lifecycle, checkpoint and game hook against the
classic, Rogue and rerelease donor implementations. Corrections were reread:
interrupted healing clears the patient's healer reservation; reaction thresholds
use maximum health; the species attack wrapper retains source enemy recovery
and rerelease cadence; classic newly selected attacks wait for the following AI
callback. No additional demonstrated defect remained in this bounded packet.

The final reviewed AI digest was
`ce7a76d302cfe1d8166b1fb2e45529b0e1345e427e6e8ec7b582893e9c5db694`;
Medic implementation digest was
`c34136b7716f786f79bfd78a1ef376e650efb132f075688e032e6a5f8d7abae3`.

Generic attack probabilities, remaining species behavior and corpse continuations,
application wiring and full shared/private saves remain open. This does not
close B11. Verification is source-only; no engine build or execution ran.
