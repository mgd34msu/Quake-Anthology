# Native Q3 chat commands

The isolated application module implements the source bodies for `say`,
`say_team`, `tell`, `vsay`, `vsay_team`, `vtell`, `vosay`, `vosay_team`,
`votell`, `vtaunt` and `gc`. It retains no chat, client or gameplay state.
The primary native client dispatcher supplies the real GAME provider and holds
its source console lease throughout dispatch.

The behavioral references are [GameCommandRuntime](https://github.com/mgd34msu/Quake-Anthology-TS/blob/main/src/content/q3/team-arena/commands.ts),
[source team locations](https://github.com/mgd34msu/Quake-Anthology-TS/blob/main/src/content/q3/team-arena/team.ts),
and the original Q3 `code/game/g_cmds.c`, `g_team.c` and `g_combat.c`.

Arguments retain the 1024-token, 1023-byte-per-token source limits. Concatenation
preserves its strict capacity comparison and possible trailing space when the
next argument does not fit. Integer arguments use the source signed-byte
whitespace rule and modular 32-bit decimal arithmetic. Chat text is limited to
149 bytes after logging. Name prefixes are limited to 63 bytes and retain the
source color and byte-25 delimiters. Voice IDs keep the full concatenated text;
the real engine reliable owner applies its own storage limit. Text is not
re-escaped before reliable command emission.

Text recipients require a live source row and CONNECTED client. Voice recipients
require a live row but have no connected-state test. Team sends compare actual
source session teams. Team mode becomes global mode below GT_TEAM. Tournament
blocks spectator text to playing clients and suppresses all voice deliveries.
Addressed text and voice echo to the sender only when the target differs and
the sender's current source SVF_BOT bit is clear. Dedicated broadcast text
uses the genuine source print sink. Source log output precedes delivery.

`vtaunt` checks the genuine enemy, then the last killed client, then the first
rewarded teammate in fixed physical-slot order, and finally global taunt.
Its target and sender outputs retain their separate current SVF_BOT gates.
Enemy and kill selection are consumed only after their ordered source outputs.
The source can inspect disconnected physical gclient rows while selecting a
taunt. Those reads must come from retained fixed source client storage.

The dispatcher calls chat and voice before score and the actual intermission
gate. At intermission the remaining command, including argv0, becomes global
chat. Only after that gate does `gc` interpret its fixed 64-client target
domain and seven source orders. Order 7 returns the TypeScript source's explicit
out-of-table error. Valid gc orders send target then sender even when both are
the same slot or the sender is a bot.

Integration remains open. The module requires the real physical source pool,
fixed native client metadata, retained source teams and taunt fields with actual
combat/death/spawn producers, current server flags, authoritative location
message formatting, distinct log output and the reliable transport sink.
The source client, pool, wire and mode owners provide these dependencies;
this module supplies no substitutes. The original C damage/death code writes
player entity.enemy, while the TypeScript player path currently omits those
writes. That source discrepancy remains an explicit integration decision.

The new translation unit belongs to `qa_application`. No configure, compiler,
test, game, parser, generator, sanitizer, benchmark or executable check ran.
The module is not yet frozen or accepted as a complete source packet.
