# Hipnotic authored paths

The native Q1 map provider now implements path_follow/path_follow2 touch and
Hipnotic path-corner behavior from the donor's
`src/content/q1/missionpacks/monsters/paths.ts`. Root read that donor body, its
BaseMonster eye helper, and the new C spawn/path/field dispatch before integration.

The follow trigger retains the original monster/decoy/cooldown checks, eye trace,
old-enemy continuation, pending walk transition, destination/yaw changes, and
two-second follow deadline. A missing destination restores the old enemy or
enters the source check-client/world/stand branch. Path corners use the authored
path name and delay. Shared target fields provide native or guest view offsets;
foreign continuation changes dispatch to the actor's owner.

The source state remains in the native monster continuation. Application
authoring must populate view offsets and bind foreign path-read/path-change
services, including the added transitions. Q1 continuation serialization and
the remaining original B10/B13 responsibilities stay open. Source review only;
no engine build, test, parser, or executable was run.
