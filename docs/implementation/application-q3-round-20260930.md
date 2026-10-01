# Application Q3 round integration

The application cut is a draft source implementation. It retains the installed
world, collision geometry, map resource, launch configuration, map revision and
outer frame revision. It captures the actual client roster, source slots, full
previous actor generations, userinfo and last accepted commands. Native clients
also carry the seven source session fields. Captured userinfo has its own owned
storage through frontend disposal, including when a rejected source client
releases its new roster record.

The cut first delivers pending event projections and captures the final roster
again. Its private active scope blocks unrelated application operations during
these reversible callbacks. The frontend marker records the restart guard at
the first actual epoch mutation. The application closes rankings and bot round
owners, retires every canonical actor, resets the genuine selected GAME source
and spawns from the retained authored map. The roster then receives fresh actor
generations in ascending source-client order.

Settlement uses four actual 100 ms source admissions. The first three precede
client reconnection. Each retained client queues the real restart command before
Connect(false)/Begin. Source rejection publishes the exact owned denial text and
then drains real client and canonical actor retirement. The fourth source frame
precedes bot resumption, ranking admission, one local source snapshot publication
and final event/network delivery. Settlement does not complete an outer frame.

The source clock retains its frame counter, time and scheduling debt. The
TypeScript oracle's `src/app/bootstrap/simulation/runtime.ts` replaces only the
GAME source in `restartSourceRound`; its host and scheduling accumulators remain
on the retained simulation. Each settlement step adds 100 ms to those accumulators
and advances the retained clock by 100 ms. GAME executes at ENTRY, while pending
restart and network observers read completed EXIT. Generic mode time must retain
the full monotonic ENTRY stamp; signed Q3 milliseconds are a separate source
field. Reconstructing that stamp from wrapped milliseconds loses its epoch.

The native compatibility flags are cleared only after successful authored
spawning and `qa_q3_maps_post_spawn`, on cvars owned by that actual provider.
Changing compatibility during initial event delivery remains distinguishable as
a full world replacement requirement before the round mutates its source.

The behavioral references are
[the TypeScript application lifecycle](https://github.com/mgd34msu/Quake-Anthology-TS/blob/main/src/app/bootstrap/application.ts),
[the retained simulation lifecycle](https://github.com/mgd34msu/Quake-Anthology-TS/blob/main/src/app/bootstrap/simulation/runtime.ts)
and the original Quake III `code/server/sv_ccmds.c` fast restart sequence. Guest
GAME reset and native transport ownership have separate source packets.

Native full ClientBegin/ClientThink, physical source-client and entity allocation,
the actual driver prepare boundary, initial ranking publication and incompatible
full world replacement are integration dependencies. A source packet review does
not establish runtime behavior. No build, compiler, executable, parser execution,
test, game run, sanitizer or benchmark was used for this draft.
