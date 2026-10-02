# Implementation and review

The user explicitly corrected the workflow: do not put review gates or test
infrastructure in front of implementation. This instruction supersedes the
review freezes and acceptance prerequisites previously recorded here.

Continue production implementation and install actual callers as soon as the
required source interfaces and bodies exist. Independent reviews may proceed
alongside implementation. A pending review, sign-off, source export, hash,
manifest, or ledger judgment must not block the next implementation step.
Do not create more review or test infrastructure.

Keep exclusive ownership of shared files to avoid conflicting edits. Coordinate
specific shared-file changes directly between their writers. Reviewers identify
the source they inspected and send concrete defects to the responsible writer.
Writers fix defects while continuing the implementation; no coordinator release
or refreeze approval is required.

Assigned feature owners have standing authority to implement necessary producer,
getter, caller, lifetime, and codec changes. Coordinate shared-file cuts directly
with their current writer; routine integration does not require another Root
approval. Root registers actual production files as they land.

Necessary verification uses the actual product and existing checks when the
implementation is ready. A partial source review does not establish runtime
correctness or project completion. Preserve the original feature scope and
report remaining functionality honestly.

Root remains the sole build-definition and publication writer. The working
tree is the recovery source. Existing temporary exports are source evidence,
not commits or a durable restart guarantee. Commit and push remain authorized
but blocked by this environment's read-only Git metadata and disabled approval.
