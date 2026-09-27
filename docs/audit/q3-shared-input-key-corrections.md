# Shared Q3 key and input ownership

The Q3 product key owner retains the two source key slots once for UI,
authorization, loose-file persistence and checkpoints. Root compared the full
implementation with donor `core/q3-cd-key.ts`: client/dedicated initialization,
validation/checksum, base read, expansion append, UI selection, generated file
bytes and archive-dirty notification. File reads and replacement use the common
filesystem owner; UI and network consumers borrow the same key state.

Q3 key-down queries read the input seat's existing held-input records. Catcher
contributions belong to individual provider owners and compose with the seat's
focus for application routing; retirement removes only the named contribution.
Root traced digital event admission and release to confirm unbound/consumed
keys also enter the existing held list.

Root rejected a separate input-seat overstrike boolean because the native
console field already owned that state. The source owner removed the duplicate
and added typed accessors to the existing text field. Q3 UI traps now borrow
that actual field, so native Insert and guest UI share one value. Root reread
the correction and the host call sites; the concrete host remains unfinished.

B25/B28 and the application input/console workflows remain open. No engine
build, parser, tests, or executable was run for this source review.
