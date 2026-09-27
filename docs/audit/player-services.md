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

Application event/UI consumers, Q3 arena unlocks, local lobby services and the
ranking-provider consumer remain under implementation. B31 remains incomplete.
No engine configuration, compilation or execution ran.
