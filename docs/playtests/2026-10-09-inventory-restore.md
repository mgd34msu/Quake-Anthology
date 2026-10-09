# Inventory restoration — THE-3166

Saved item values resolve by item identity. Declaration order, label text, and
optional callback shape do not define the saved gameplay state. The shared
inventory reader binds the current source catalog and callbacks and writes the
saved count, capacity, and policy through the current value writer. It preserves
specific resolver errors instead of replacing them with a generic owner error.
Missing saved items still produce a load error.

The duplicate positional declaration checks were removed from Q1, Q2, Q3,
QuakeC, native Q2, guest Q3, and mode inventory resolvers. Mode reconnection checks
an existing lease directly; it no longer allocates and rebuilds the catalog to
re-prove the saved values. Original objective reconnection remains in place.

The actual old Q2 rerelease save contains 63 native item values and 62 source
declarations. Thirteen declarations changed positions in the current catalog.
Its component restore now preserves every value, the saved serial and revision,
and the action callback. Additional checks cover changed live metadata, an added
declaration, missing items, a source binding with different optional callbacks,
and one action invocation. The existing 325-byte writer fixture is unchanged.
Both compilers' strict translation-unit checks passed.

Evidence: `/tmp/qa-the3166-inventory-20261009/receipt.json` and
`/tmp/qa-the3166-inventory-build-20261009`. Full old-session restoration also
requires the historical event import; this component proof does not establish
that the entire saved game loads.
