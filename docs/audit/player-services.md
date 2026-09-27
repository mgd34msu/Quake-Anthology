# Player services implementation

The application-owned progress store reads and writes the donor's version 1
`player-progress.json` using the shared JSON and filesystem services. A retained
string table and hash index deduplicate `(source, participant, event)`; later
events with that identity preserve the first result. Counted UTF-8 strings retain
embedded NULs, and filtered enumeration borrows stored records without copying
them. Q1/Q2 achievements and level completions and all three games' match scores
retain the donor's event fields and source restrictions.

Records publish only after atomic file replacement succeeds. The filesystem can
report an error after rename if directory synchronization fails, so the next
mutation reloads the file before choosing whether to insert. Recovery copies
the incoming event before reloading, including when it borrows the old store.
Reload builds a candidate table and leaves existing state intact on failure.
One application writer owns each profile file.

An independent reviewer read all four progress files, the complete donor
`app/bootstrap/player-progress.ts`, and the shared UTF-8 correction. It found
no additional demonstrated defect. Root self-review caught that the existing
UTF-8 iterator repairs malformed input; a strict predicate now shares its ICU
decoder and validates events before duplicate admission. The existing repairing
iterator keeps its behavior.

The shared numeric formatter also gains fixed precision output for bot goal
diagnostics. Root read the complete addition: it uses the existing C locale,
temporarily selects ties-to-even rounding, restores thread rounding/locale,
handles nonfinite text and checks output capacity. This is source review only.

Q3 arena progression uses the selected source's existing archived cvars rather
than a separate score store. It handles best ranks across five skills, training
and final arenas, tier availability, medals, movies, reset and unlock commands.
An independent reviewer compared the complete implementation with the donor's
`base-arena-progression.ts`, including signed award wrapping and source cvar
notification order. The shared Q3 info-string writer now removes the first exact
key and preserves source prepend/large-buffer append behavior; lookup uses ASCII
case folding. Its existing checked failure contract leaves input unchanged.

The native local lobby registry and retained membership owner are implemented.
They preserve prepared composition, readiness, seat capacity, host publication,
joining, match generations, completion/return and leave. Room versions share
immutable composition bytes and interned strings; membership copies only when
a retained reader needs the old version. Synchronous callbacks run from the
application transition queue with retained views. Failed host publication
attempts both room reset and bound-host teardown.

An independent reviewer read all three lobby files and both complete donor
owners, plus shared string/address/whitespace contracts. It found no confirmed
defect after checking copy-on-write allocation, mutable-registry callback
lifetime, stale generations, poll counters and cleanup. The composition must
already be admitted; account authentication remains a separate service.

The shared ranking-provider lifecycle is implemented: optional begin, account
login/create and join, typed integer/string reports, poll, reset/spectate,
disconnect and complete cleanup. Source slots resolve to independent 64-bit
account IDs at flush; -1 denotes the match. Provider failure exposes unavailable
state. End attempts all active logouts and finalization despite earlier failures.
Single-player and disabled ranking never call the provider. No backend is invented.

An independent reviewer read the complete lifecycle and donor `rankings.ts`,
including denial versus failure, duplicate-account admission, notification order,
disconnect-finally and end cleanup. It found no confirmed bounded defect. Native
gameplay emits typed source reports to the application queue rather than invoking
providers during simulation; those actual producers/queue consumers are ongoing.

Application event/UI consumers, arena archive transactions and round deduplication,
actual lobby transition consumers, account and local ranking services, and the
ranking-provider application consumer remain under implementation. B31 remains incomplete.
No engine configuration, compilation or execution ran.
