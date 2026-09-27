# Q2 player and item interoperability corrections

This source integration extends B11/B13 and keeps their original completion
criteria open. It does not establish compiled or runtime behavior.

Player listings, chat, obituary lookup, chase selection, co-op all-dead checks,
and the global living-player check now consume the shared player projection.
Score reads and writes use the same explicitly selected mode context. Q2 squad
spawn candidates retain Q2 private state as required by the donor rerelease
player code; the global living-player test is shared across families.

Item admission retains source give policies and supplemental item definitions.
Console pickup grants use the shared pickup path. End-unit inventory cleanup
walks actual shared entries, including custom Q2 keys. Auto-shield preference is
separate from current powered-armor activation and survives player carry state.
Map items expose authored target fields through the shared target owner.

Root source review found two stale inventory-only field accesses and incomplete
validation of the supplemental item callback group. The owner corrected them.
Root also required actor/state reacquisition after foreign callbacks in target
anger, co-op death, and squad respawn; those guards were reread before integration.
The proposed exclusion of food cubes and packs from give-all was refuted by
the donor command filters and item definitions, so no extra restriction was added.

The changes also retain the rerelease world-text byte conversion and correct
debris/barrel source rules. Target anger requires explicit foreign classification
and monster-control bindings instead of silently ignoring foreign actors.

Application bindings for the shared player projection, selected mode score
context, foreign movement, target anger, and supplemental content are still
required. Q2 AI/locomotion, summons, and other gaps in the original audit remain
open. No engine compilation, tests, or execution were performed.
