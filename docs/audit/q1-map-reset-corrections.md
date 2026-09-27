# Retained Q1 map reset and water transition

Q1 now exposes an explicit begin-map operation after shared actor retirement.
It checks the session safe point, empty registry, completed native release
fanout, absence of borrowed queries, and interned map identity before mutation.
It replaces borrowed map services, clears clocks and map references, releases
door groups, and reuses cleared actor/player/map pools. Provider resources,
random state and attack sequence survive; the application owns player carry
and campaign data. Root compared the reset with the complete game/map owner
structures and release fanout.

Native Q1 and NetQuake movement now share one pure water-transition decision.
Root compared it with movement/q1/water-transition.ts and the native entity
service checkWaterTransition. The native adapter also preserves the source
host's six-material normalization. Splash dispatch precedes state publication,
and native actor generation is reacquired afterward. Initialization and negative
exit water levels remain meaningful.

These are source-reviewed hooks for application integration. Their existence
does not establish complete map travel or all Q1 save behavior. No engine
compilation or execution was performed.
