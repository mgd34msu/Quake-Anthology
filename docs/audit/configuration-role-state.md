# Provider role changes retain private state

The application publication review found that the configuration identity included
the selected role mask. Changing an instance's routed roles therefore constructed
a new implementation owner, discarding its private continuation even when its
implementation, content and options were unchanged.

`transaction.c` now separates each snapshot's instance view from its retained
implementation owner. The identity excludes the role mask; reused owners keep
their private state and resources while each snapshot exposes its own selected
roles. Old readers retain their old routing view. Construction hooks still receive
stable owner metadata, whose role mask describes the initial selection only.
Application routing must read the current snapshot.

The root reviewed reference release, identity construction and both new/reused
instance publication paths. The application owner independently reviewed this
change and its retained prepare-hook metadata pointer; no defect was reported in
that bounded review. This is source review only. Runtime qualification and the
complete B15 application publication requirement remain open.
