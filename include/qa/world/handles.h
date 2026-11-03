/*
 * Closure handles for fn+ctx closures a callee may keep and call after the
 * closure's owner retired it. TS closures stay callable forever (the GC keeps
 * their state) and throw "closed" once retired; a C ctx pointing at freed or
 * stack memory is undefined behaviour. A handle is an opaque ctx token that
 * never repeats (a 64-bit serial). While open it resolves to its state; once
 * closed it resolves to NULL, so the caller raises the TS "closed" error.
 * Closed handles hold no memory. Single-threaded: the world simulation runs
 * on one thread. GPL-2.0-or-later.
 */
#ifndef QA_WORLD_HANDLES_H
#define QA_WORLD_HANDLES_H

/* Registers state and returns its token. Aborts only on OOM. */
void *qa_world_handle_open(void *state);
/* The registered state, or NULL once the handle is closed. */
void *qa_world_handle_get(const void *token);
/* Idempotent. */
void qa_world_handle_close(const void *token);
/* Number of open handles (tests). */
unsigned long qa_world_handle_open_count(void);

#endif
