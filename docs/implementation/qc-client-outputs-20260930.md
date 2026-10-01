# Qualified QC client output owner

The source reference is [Quake Anthology TypeScript](https://github.com/mgd34msu/Quake-Anthology-TS).
The complete declaration and lease contracts are in
`src/contracts/mod-client-outputs.ts` and
`src/world/session/mod-client-outputs.ts`. Actual output claims register input
application activity in `src/app/bootstrap/simulation/runtime.ts`.
The QC admission, publication and restore boundaries are in
`src/compat/qc/mod-provider.ts` and `mod-clients.ts`.

`guest_qc_qualification.c` now admits the four declared output channels. It
qualifies each original field against the loaded program and declared actor
authority. Body shape requires the actual declared source mins and maxs.
Client flag outputs require an explicit source-private mask. Scalar mappings
retain their exact finite declaration values, reject duplicates, and validate
all masked values against the original unsigned word.

`guest_qc_outputs.c` owns real lazy channel claims on the source engine and
detached publications on its reserved clients. A claim requires a live full
actor generation, the exact original BORROWED client slot, canonical roster
membership and the selected movement's genuine capability gate. It rejects
channels already held by another retained QC engine. Every channel value is
validated before the actor publication changes.

Original admission marks the source client admitted before its callbacks,
seeds its source body bounds from the canonical spawn and publishes afterward.
Successful original invocations republish clients already owned by the lease.
Declared source output vectors retain their original field authority instead
of being overwritten by canonical projection or prematurely changing the
shared collision body. Disconnect and actor release clear that actor's values.
The engine retains its channel claims until successful teardown, including
when it has no remaining clients. The global activity query reads actual
retained claims and qualified input subscriptions.

The private engine codec is version 7. It records the claimed channel mask and
publication membership beside the actual reserved client identity. Restore
validates the exact declaration digest and guest slot bindings, then reads the
restored original fields with projection suppressed to rebuild detached
values. It neither executes guest code nor invents canonical roster entries.
The existing final provider recapture, after roster restoration, qualifies
canonical actor and seat ownership and selected movement capabilities again.
Earlier private codec versions are rejected. Version 7 also retains the actual
classic QW prepared stage and validates its real reserved source ownership.

The pure `application_qc_control_outputs` reader merges only actual live
publications from attached source owners. It returns no invented values for
missing channels. The movement owner supplies
`application_control_output_admit` and the actual consumers of view offset,
movement mode, stance and body shape. Their complete source integration and
independent acceptance are required separately. Other source runtimes do not
acquire QC claims through this owner.

The same source caller rotation repairs two existing producers. Classic QW
bind, reserve and canonical setinfo update the original stable
`qw-name:<physical-slot>` engine buffer with capacity 32, preserving its
31-byte name extent. NQ retains its original allocation policy. Actual QC
`changelevel` calls the admitted source travel producer rather than entering
the public application queue boundary during a source turn.

Verification is limited to complete source reads, manifest hashes and scoped
whitespace checks. No builds, tests, parsers, game runs or other executable
validation ran. This packet does not claim whole control, campaign or runtime
parity. Root must register `guest_qc_outputs.c` and the previously accepted
canonical QW userinfo getter TU in the application target.
