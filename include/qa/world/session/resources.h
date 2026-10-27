/*
 * Port of src/world/session/resources.ts: session resources and the
 * reverse-order ResourceScope. TS plain Error throws abort with the
 * identical message. TS close() collects cleanup failures into an
 * AggregateError; C cleanups abort on failure directly (they cannot
 * throw recoverably), so the first failure is fatal here.
 * Scopes are heap objects; close() mirrors TS close, destroy() also
 * frees. The scope name is copied. GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_RESOURCES_H
#define QA_WORLD_SESSION_RESOURCES_H

#include <stdbool.h>
#include <stddef.h>

/** TS SessionResource. */
typedef struct qa_session_resource {
  void *ctx;
  void (*close)(void *ctx);
} qa_session_resource;

/* A TS `() => undefined` cleanup closure: fn ptr + ctx. */
typedef void (*qa_session_cleanup_fn)(void *ctx);

typedef struct qa_resource_scope qa_resource_scope;

/* Returns NULL only on allocation failure. */
qa_resource_scope *qa_resource_scope_create(const char *name);
void qa_resource_scope_destroy(qa_resource_scope *scope);
const char *qa_resource_scope_name(const qa_resource_scope *scope);
bool qa_resource_scope_is_closed(const qa_resource_scope *scope);
/* TS assertOpen(): aborts with "<name> is closed" when closed. */
void qa_resource_scope_assert_open(const qa_resource_scope *scope);
/* TS own(): defers resource.close and returns the resource. */
qa_session_resource qa_resource_scope_own(qa_resource_scope *scope,
                                          qa_session_resource resource);
void qa_resource_scope_defer(qa_resource_scope *scope,
                             qa_session_cleanup_fn fn, void *ctx);
/* Idempotent; runs cleanups in reverse acquisition order. */
void qa_resource_scope_close(qa_resource_scope *scope);
/* The scope viewed as a SessionResource (TS implements it). */
qa_session_resource qa_resource_scope_as_resource(qa_resource_scope *scope);

#endif
