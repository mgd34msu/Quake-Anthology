# Native bot checkpoint restore

This is the same-binding private continuation packet for B26. Application saves
that reconstruct actors, library resources, navigation and session RNG remain
separate, unfinished work.

The previous `qa_bots_restore` captured the current population, then restored bots
one at a time. Restoring goal weight mappings, chat capacity and weapon selectors
could allocate after another bot's state had already changed. Its recovery used
the same allocating restore path, so a second allocation failure could leave the
population partially restored. This is confirmed by the source control flow;
allocation failure has not been executed.

The replacement prepares cloned weights, goal mappings, weapon selectors, input
bindings and the final shared chat capacity before publishing restored state.
The source-visible callback phase validates saved movement edges. Runtime,
goal, movement, action and chat owners reject mutation or destruction while the
checkpoint operation borrows them. A checkpoint itself has a retained operation
lease; a callback's destroy request is deferred and causes restore rejection.
Actor retirement marks the population entry retired and fails final binding
validation before commit.

After preparation, commit transfers prepared mappings/selectors and copies
movement, action, AI and timing state. The commit paths allocate nothing and call
no external services. Chat commit first returns all affected queues to the shared
pool, then writes the saved queues from the reserved pool. Unaffected chat states
keep their queues. Failure releases only prepared resources; no allocating
rollback is needed. Retained pool capacity may grow during a failed preparation,
but the live queues and restored continuation values remain unchanged.

The private preparation interfaces live in `src/bots/checkpoint_internal.h`.
Implementations remain in existing owner modules, so this adds no CMake source
registration requirement or public transaction API. Existing per-owner restore
entry points remain available; movement now shares edge validation, and chat now
shares the callback-free queue replacement kernel.

Source checks completed:

- Read all changed paths and traced preparation, commit and discard branches.
- All eleven identities in `bots-checkpoint-20260929.sha256` matched.
- All twenty identities in the updated `bots-20260929.sha256` matched.
- `git diff --check` for bot source and packet documentation returned exit 0.

The packet is frozen for the independent foundations reviewer. No configure,
build, compiler, parser, executable, test, game, sanitizer or benchmark was run.
Compilation, allocation-failure execution and restored gameplay behavior are
unverified. This packet does not establish B26 or baseline completion.
