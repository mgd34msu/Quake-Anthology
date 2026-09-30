# Q2 pickup observation source review

The reviewer read the complete new `src/gameplay/q2/items/observation.c` and
the affected registration, release and reconstruction paths in item lifecycle
and checkpoint code. The shared offer/eligibility code in `items/touch.c`,
native benefit branches in `items/grants.c`, missing-entry admission in
`items/powers.c`, and source supply selection in `items/internal.h` were read
to compare the observation with actual grant behavior. The reviewer also read
`qa_q2_run_actor`, provider destruction, actor retirement/reuse, frame entry,
`q2_checkpoint_idle`, and the session advance/safe-point admission.

## Confirmed high defect and correction

Before correction, `src/gameplay/q2/game.c` frame entry reclaimed retired
actors while a direct source invocation could still borrow them. The new
observer enters `qa_q2_run_actor`; that sets `current_actor` but does not set
the session invocation, scheduler, notification or stepping guards. A source
traits or selected preview callback can therefore invoke
`qa_session_advance` while the session is otherwise idle. Frame entry then
moved retired actors into the spare list; a subsequent source spawn could
reuse and clear the item storage that the outer observation still borrowed.
The selected preview and native preview retain the original item pointer.
Provider checkpoint/destruction admission alone did not prevent that frame
entry path.

The Q2 owner corrected `game.c:162` to reject frame entry while
`current_actor.registry` is nonzero. The reviewer reread the correction:
the guard runs before release-error handling, reclamation and provider clock
mutation. `qa_q2_run_actor` restores the previous actor on every callback
return. This closes the traced frame-reuse path for the entire synchronous
inspection, including nested source invocations. No runtime reproduction was
performed; confirmation and correction acceptance are from the source trace.

## Bounded result

No additional confirmed defect was found in the inspected offer eligibility,
instanced cooperative ownership, native supply preview, regular armor result,
observer registration, exact actor retirement and reconstruction paths.
Inspection performs no source grants or inventory configuration. Pure selected
preview executes inside the source invocation borrow. Missing inventory entries
are represented with the same native definition capacity used by grant admission.
Objective/source-specific usefulness estimates are not exact tactical scores.

The reviewer did not audit every Q2 item, power, monster or entity operation.
This is not whole-Q2 or full interoperability acceptance. No compiler, parser,
build, executable, test, game, benchmark or sanitizer was run.

Current identities read after the correction:

```text
14a093c361dbe24ef1dc75899cf470f1329b40b7e358e6768cd8fd7a0fed1318  src/gameplay/q2/items/observation.c
e5275cf8dce6d95ab090feefe7ff531dceecc68439097bdac0e81672747b9e56  src/gameplay/q2/game.c
```

These identify the new observer and correction boundary. They do not freeze
uninspected portions of the broader Q2 work in progress.
