# Q1 addon trigger and corpse peer review, 2026-09-29

Source inspection only. No compiler, configuration, parser execution, test, executable, gameplay or benchmark was run. No defects found in the bounded paths below after comparison with the donor. This does not accept all Q1 functionality, CTF gameplay, save compatibility or the baseline.

## Inspected scope

Read complete `maps/addon_triggers.c`, complete donor `addons/triggers.ts`, complete native `maps/special.c` and donor `addons/corpses.ts`. Compared the implemented fifteen controllers with donor foundation `multiFire`/`multi_wait`, addon trigger initialization and coop/rune removal helpers. Read the exact CTF base-monster removals in donor `addons/ctf/maps.ts` and registration in `addons/ctf/index.ts`; compared native CTF removal and addon predicate hunks in `monsters.c` and `maps/triggers.c`. Read complete `queries.c` and the affected snapshot reservation contracts. Inspected native multi-enable/multi-fire retirement repairs, new kind/action declarations and dispatch, source skin propagation and affected checkpoint map fields/action-kind validation.

The new unfrozen `maps/addon_campaign.c` and axe-button initialization in `maps/movers.c` are excluded. Complete monster logic, all existing special/triggers behavior, CTF voting/travel/scoring/equipment, full checkpoints, complete target registry and every query consumer are outside this acceptance. Reading the complete donor trigger module does not mean every controller in that module has been implemented.

Matched identities, relative to `src/gameplay/q1` except the public header:

| Path | SHA256 |
| --- | --- |
| `maps/special.c` | `e0d36c9cb0120c1f972497dce91ccae8637517999dc0f0995fcdf4ca87e5b9c7` |
| `maps/addon_triggers.c` | `ebd96bb598c0396eacdad6a990fad02f948ea4a50faec5cb779d163d253ff424` |
| `maps/triggers.c` | `cc94eea3fa758837c4606fb78699d175b0e783356e858626166d7b91d120ae76` |
| `monsters.c` | `9c0942d4c98d01b4e53caaf72c31cf69e008ed8e75fad65ce106f4fe927b76ba` |
| `queries.c` | `6a77d4ec529d47753e0260eb5c3c8154068b104585f0e6933270d87e64749152` |
| `maps/runtime.c` | `e14e6ae655b2c92283913f68470467c464a1eac8c55ea7b998d74d9c95dc6285` |
| `maps/internal.h` | `1b7487477102143d3921fe7b11ec77f2bdfdc2fb7459c1acf328980f33b4a83c` |
| `checkpoint/map.c` | `8af1d507cd740f97557e7a87fd031f81fb47cfce4b0bb79a82121c38d9ba4ce9` |
| `include/qa/game_q1_maps.h` | `ae9c8ade3bb06f18315c87cde5a1da54f0d5dcc794d0c6f8b641392ebb4c0b7e` |

## Source traces

Counters retain source decrement/message/reset distinctions. Timed counters cancel their reset before shared multi-fire and retire after completion; ordinary reusable counters restore their count separately from the multi-fire wait deadline. Repeater toggles own one scheduled source action, and preserve target callbacks before random rescheduling. Multitouch/sacrifice contact windows use the shared scheduler and exact entry/exit restrictions. Explosion clears target delay before firing, preserves source owner/ignore attribution and uses the existing colored effect convention for MG3 code244/count3. Target changers call the actual registered target setter. Cleanup and kill relays inspect canonical monster/combat traits, and stop after source retirement. Rune and nightmare relays preserve their actual campaign masks. Distinct coop/rune removal rules follow the source registration and initialization helpers.

The corpse pose table matches all38 donor entries. Corpses stay live and nonsolid, while addon ambience emits static publication and retires; generic sound/volume/attenuation and empty-sound removal match the source. The static helper reacquires full actor identity after body/static callbacks. Native CTF removes the exact16 base-monster class names before physics admission, and the changed addon predicates exclude the CTF enum.

Snapshot acquisition marks the selected retained slot borrowed before external player/observation callbacks; failure releases that reservation. Nested callback queries therefore choose another retained slot. Target snapshots reserve capacity and mark the slot borrowed before target iteration callbacks. The reused multi-enable and multi-fire paths reacquire the native actor/map after initialization and combat damage-admission callbacks. The previously reviewed operation lease remains the owner lifetime boundary for these public/scheduled paths.

New action-kind predicates reject foreign continuation actions, including during map checkpoint decoding. Count, wait, target identifiers, cooldowns and scheduler actions use existing typed fields rather than parallel state. No whole codec or restoration-parity claim follows from those inspected hunks.

## Remaining integration

The owner's listed door/group, lore/music, heal/quad, silent teleport, cinematic, skill and explosion-repeater controllers remain outside this packet. Frontend colored explosion consumption and actual source gameplay are unverified. Root-owned CMake registration and final application integration must be checked separately. This report is bounded source acceptance, not graph-task completion.
