# Corrections found during integration

These findings extend the original audit. They do not replace its criteria or
close the corresponding baseline tasks. Review here is by source inspection;
the engine has not been configured, compiled, tested, or run during recovery.

## Storage identity and staged equipment admission

Original tasks: B06, B09, B11, B14.

Actor identity alone did not distinguish body or combat storage replaced by a
provider callback. Commit `07aaae9` gives each storage binding an identity and
rejects a stale callback result after replacement. Mode rollback can now remove
only storage it created. The modes owner independently reviewed this change.

Equipment admission could write ammunition before a later native admission
failed. Commit `93f8146` adds prepared inventory admission and Q2 hand-grenade
state. Inventory preparation retains the store, preserves existing values, and
reserves space for missing native items. Supplemental native entries share the
canonical inventory even when its primary provider is foreign. Commit publishes
the prepared entries without allocation or provider calls. Ordinary gameplay
inventory operations continue through their existing hooks.

Definition replacement now allocates the replacement before retiring its prior
lease. Explicit foreign overrides retain priority over refreshed native
definitions. Source snapshots distinguish native supplemental entries from
external primary entries and retain the primary inventory even with no item
groups.

Independent review found that enumeration initially checked revisions only at
the end, allowing another indexed provider call after a callback changed the
entry layout. The shared count and entry callback helpers now reject a revision
change immediately. Review also found that Q2 validation called
`qa_world_body_read`, which could invoke a provider after inventory publication.
Q2 preparation now retains the body storage identity; validate and commit check
that identity without reading through the provider. Both corrections were
independently re-read before the commit.

The modes integration coordinates inventory, Q2 and Q3 preparation. Its final
publication path and application consumers remain separate work; these commits
do not establish complete B09 or B14 functionality or runtime acceptance.

## Filesystem integration findings

Original tasks: B03, B15.

Root review of the proposed portable filesystem found a duplicate `descriptor`
declaration in the Linux `open_contained` branch. The Windows writable traversal
closed validated directory handles and later resolved paths again for file
creation, replacement and removal. A directory replacement between those steps
could redirect the operation. Both findings were returned to the source owner;
the proposed chunk is not accepted or committed on this evidence.

## Jev hook repair evidence

The plugin source checkout contains local commits `bfe2bbe` for loading the
existing shell environment key and `bef6085` for recognizing namespaced Codex
collaboration tools. The latter passed the plugin's ten hook tests. These were
plugin tests, not engine checks. The repaired runtime and hook matcher were
installed locally.

A subsequent environment-stripped installed-launcher probe reported no usable
key. Inspection showed its earlier key-loading stanza was absent again; the
cause of replacement is still under investigation. The persistent checked
adapter loads the existing key without printing it and continues to obtain
judgments. Automatic hook delivery is not established by these explicit calls.

An explicit namespaced brief probe recorded rejection
`v_e2514f002fc24e2bafc1f07b901cc989` for perceived scope deferral. A revised brief
stated the unchanged existing task obligations and recorded an allowed judgment
`v_7ea0522c400749028043f2a2179a7ecb`. Neither verdict accepts implementation
source. The original rejected evidence remains in the ledger.
