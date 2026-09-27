# Prepared provider admission and stable-world travel

The application cannot prepare a configuration by calling native constructors
that immediately register live callbacks. Q1 construction now retains private
state only; its component and combat policy are borrowed descriptors admitted
by the application. Destruction follows detachment and actor-release forwarding.

Session component preparation reserves a component slot and the matching
scheduler slot without publishing either. A nonzero retiring owner identifies
the exact active slot being replaced, including a different owner at full
capacity. Duplicate new owners and reused retirement slots are rejected. Old
registration remains available until explicit removal. Removal preserves the
reservation; commit requires the old slot empty. Registration order is assigned
at commit, with exhaustion capacity reserved during preparation.

Combat policy preparation reserves array capacity and provider identity without
exposing a policy. Existing same-owner policy must be removed before commit.
Successful commit consumes each token without allocation or provider callbacks;
failure leaves it abortable. Abort releases only prepared state. Parent services
reject destruction while admission tokens remain outstanding.

Geometry preparation stores candidate spatial topology privately while actors
continue using the current geometry. Commit requires an empty actor registry,
fully forwarded body releases, and no active spatial visits/callbacks. It swaps
the borrowed geometry and fixed spatial sectors without replacing the shared
world pointer. Retained native providers therefore keep valid world borrows.

The explicit session retirement operation releases actors, clears scheduled
work, and resets provider clocks while retaining world ownership. Actor-release
failure faults the session after committed retirements and cannot restore those
actors. It shares the retirement implementation with world replacement.

The application owner independently read the root-written session, scheduler,
combat and world changes, requested explicit retiring-owner reservation for
full-capacity replacement, and rereviewed that correction. No further defect was
found in this bounded source pass. The application transaction, source-specific
map state, role registrations and save consumers remain required under the
original B07/B09/B15/B34 criteria. No engine code was built or executed.
