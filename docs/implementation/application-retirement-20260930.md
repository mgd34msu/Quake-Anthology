# Application retirement, 2026-09-30

This B34 packet connects the existing `qa_configuration_hooks.retire` callback to application teardown. Configuration destruction keeps its current snapshot until this callback succeeds.

`application_publication_retire` stops on failed map-service retirement before clearing the world. Failed world or component retirement keeps the provider array and attachment flags for retry. Successful retirement releases routing, equipment, modes, and the provider array before configuration destruction closes snapshot owners. Publishing a null candidate now completes publication bookkeeping without repeating source shutdown.

The Q1 worker independently inspected the actual diff and the destruction path in `src/session/configuration/transaction.c`, including retry after partially detached policies or components. No confirmed defect remained in that bounded source review. `git diff --check` passed for the three changed source files.

No build, test, executable, sanitizer, or benchmark ran. B34 and BASELINE remain incomplete. Native Q2 lifecycle admission and broader application integration are separate packets.
