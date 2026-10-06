# Custom-game playtest

MIKE-09 and MIKE-10 pass in the actual `qfiles/qa-c`. The drawn monster selector exposes all 396 creature choices from the sixteen installed Q1/Q2 content sets without unavailable reasons. Ten CPU/GL runs used the public menus to configure and start these sessions:

| Map content | Donor selection | Native monsters | Renderers |
|---|---|---:|---|
| Q1 `e1m1` | Q2 monsters | 23 Q2 | CPU, GL |
| Q2 `base1` | Q1 monsters | 19 Q1 | CPU, GL |
| Q1 `e1m1` | Q2 arsenal | 23 Q1 | CPU, GL |
| Q1 `e1m1` | Q3 arsenal | 23 Q1 | CPU, GL |
| Q1 `e1m1` | One authored class changed to Q2 Soldier | 23 mixed Q1/Q2 | CPU, GL |

Every run reached 108 presentation observations, advanced native monster animation, accepted normal player movement, and quit normally. Arsenal sessions retained the actual selected donor and its admitted inventory. The per-class roster sessions contained monsters from both native providers together. Q3 bots are separate from the Q1/Q2 creature selector.

The fixes address shared composition boundaries. `fd9ca798` configures native Q2 entity/client capacity before every provider spawns. `b16d1754` reads a player's selected arsenal for presentation. `e2c936c8` and `7c50434a` route Q1 monster target queries through the unified physical player roster and propagate callback failures. `fe1c0a07` removes the query's remaining dependency on a native Q1 networking object: its selected-eye cache now belongs beside the existing gameplay visibility cache. Donor-only Q1 monsters use the same cyclic/PVS algorithm without creating another networking runtime.

The reverse-monster and per-class sessions used shipped revision `fe1c0a07`; the other sessions used `09bf85f7`. [Recorded observations](2026-10-05-custom-games.json) identify every run. The actual earlier reverse-mixture failure and incomplete private observation attempts remain in the private archive. A stale GL selector screenshot is excluded from visual evidence; availability was checked from the actual drawn-menu records and the corresponding CPU image.

These checks qualify availability, starting, native animation, movement and the selected inventory in the listed compositions. They do not prove every creature's attacks, every weapon's firing, all possible combinations, audio or performance. Functional references are Quake's `SV_CheckClient`/`SV_NewCheckClient`, Quake II's pre-spawn entity capacities, and the unified selections described by the TypeScript menus. No game assets are included.
