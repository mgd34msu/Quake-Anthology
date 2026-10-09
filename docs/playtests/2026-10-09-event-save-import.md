# Existing event records — THE-3166

Earlier saves retained delivery counters before their durable event rows. After
those counters were removed, the reader treated the first old counter as today's
persistent-event count. The owner's Q3 save consequently interpreted 690800 as a
row count and rejected the file as truncated.

One file-boundary normalizer imports the historical prefix and envelope fields
into the existing durable row codec. It consumes retired delivery records without
recreating their runtime queues. The current writer is unchanged; there is no
format selector or new schema tag.

GCC and Clang strict compilation and ASan/UBSan component checks passed. The
actual old Q3 record retains its owner; the old Q2 rerelease record retains all
70 persistent records and its owner. Each normalized record decodes, rewrites,
and decodes through the current codec with a byte-identical compact rewrite.
Actual historical writers also exercise nonempty transient queues and the
intermediate header. Truncated inputs and failed payload/slot admission leave
the existing store intact.

The tested inputs each have exactly one complete structural interpretation.
Untagged arbitrary bytes cannot guarantee this universally; ambiguous input is
reported as a load error instead of being guessed. This component proof does
not establish complete saved-session restoration.

Evidence: `/tmp/qa-the3166-q3-events-20261009/report.md` and
`/tmp/qa-the3166-events-build-20261009`.
