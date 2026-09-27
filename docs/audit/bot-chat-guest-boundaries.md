# Shared chat state and Q3 memory boundaries

The native chat algorithms now accept counted external text/match accessors for
guest memory. The Q3 host supplies address validation, signed-byte match-offset
encoding and observed writes; native callers retain direct buffer access. The
shared implementation preserves partial match writes, failed-match fields,
whitespace copy order and synonym snapshots without a second chat engine.

The bot implementation owner independently read the complete shared string
implementation, new access contracts and concrete Q3 chat dispatcher against
the donor dispatch and algorithms. That bounded review found no additional
confirmed defect. The host dispatcher remains part of its separate integration
packet; this commit integrates the shared chat library changes.

Root read all state, construction, asset and header changes against the donor's
chat-file load, initial-chat selection, identity, message output and console
queue paths. Root found that a zero-capacity pre-setup owner would allocate an
unbounded console pool. The correction uses an explicit unavailable-pool state;
queueing then reports the source diagnostic without adding a message. Ordinary
native zero-capacity configuration retains its existing unbounded meaning.
Root reread that correction and the checkpoint's aggregate capacity admission.

Initial-file replacement tracks state revision across callbacks, retains state
lifetime, and distinguishes cached loads from fresh integrity checks. Output
consumes its message only after a successful write. Missing debug chat types
and exhausted console pools follow the source diagnostics.

The concrete bot runtime, host integration, complete decisions/roster and
application consumers remain unfinished. This does not close B23 or B26.
No engine compilation or execution ran.
