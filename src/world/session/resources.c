/* Port of src/world/session/resources.ts. GPL-2.0-or-later. */
#include "qa/world/session/resources.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct qa_resource_scope {
  char *name; /* owned */
  struct {
    qa_session_cleanup_fn fn;
    void *ctx;
  } *cleanups;
  size_t count;
  size_t capacity;
  bool closed;
};

static noreturn void qa_errorf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "Error: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  abort();
}

static void *xmalloc(size_t size) {
  void *ptr = malloc(size != 0 ? size : 1);
  if (ptr == NULL) abort();
  return ptr;
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}
qa_resource_scope *qa_resource_scope_create(const char *name) {
  qa_resource_scope *scope = malloc(sizeof *scope);
  if (scope == NULL) return NULL;
  scope->name = malloc(strlen(name) + 1);
  if (scope->name == NULL) {
    free(scope);
    return NULL;
  }
  strcpy(scope->name, name);
  scope->cleanups = NULL;
  scope->count = 0;
  scope->capacity = 0;
  scope->closed = false;
  return scope;
}

void qa_resource_scope_destroy(qa_resource_scope *scope) {
  if (scope == NULL) return;
  qa_resource_scope_close(scope);
  free(scope->cleanups);
  free(scope->name);
  free(scope);
}

const char *qa_resource_scope_name(const qa_resource_scope *scope) {
  return scope->name;
}
bool qa_resource_scope_is_closed(const qa_resource_scope *scope) {
  return scope->closed;
}

void qa_resource_scope_assert_open(const qa_resource_scope *scope) {
  if (scope->closed) qa_errorf("%s is closed", scope->name);
}

void qa_resource_scope_defer(qa_resource_scope *scope,
                             qa_session_cleanup_fn fn, void *ctx) {
  qa_resource_scope_assert_open(scope);
  if (scope->count == scope->capacity) {
    scope->capacity = scope->capacity != 0 ? scope->capacity * 2 : 8;
    scope->cleanups =
        xrealloc(scope->cleanups, scope->capacity * sizeof *scope->cleanups);
  }
  scope->cleanups[scope->count].fn = fn;
  scope->cleanups[scope->count].ctx = ctx;
  scope->count++;
}

struct owned_resource {
  qa_session_resource resource;
};
static void close_owned(void *ctx) {
  struct owned_resource *owned = ctx;
  owned->resource.close(owned->resource.ctx);
  free(owned);
}

qa_session_resource qa_resource_scope_own(qa_resource_scope *scope,
                                          qa_session_resource resource) {
  struct owned_resource *owned = xmalloc(sizeof *owned);
  owned->resource = resource;
  qa_resource_scope_defer(scope, close_owned, owned);
  return resource;
}

void qa_resource_scope_close(qa_resource_scope *scope) {
  if (scope->closed) return;
  scope->closed = true;
  while (scope->count != 0) {
    scope->count--;
    qa_session_cleanup_fn fn = scope->cleanups[scope->count].fn;
    void *ctx = scope->cleanups[scope->count].ctx;
    fn(ctx);
  }
}

static void scope_close_ctx(void *ctx) {
  qa_resource_scope_close((qa_resource_scope *)ctx);
}
qa_session_resource qa_resource_scope_as_resource(qa_resource_scope *scope) {
  qa_session_resource resource = {scope, scope_close_ctx};
  return resource;
}
