/*
 * Port of src/core/cvars/mirror.ts (SharedCvarMirror): selected engine
 * controls share values while each client retains its own unrelated
 * settings. The TS imports CvarRegistry type-only from cvars/index
 * (not yet ported); the registry surface this file actually calls
 * becomes the callback interface below, and the WeakMap of shared
 * subscriptions becomes an explicit list keyed by owner ctx (entries
 * leave only through close, since C has no weak references).
 * Session identity is an opaque token compared by pointer, dialect
 * by string, mirroring the TS comparisons. The foundation
 * CleanupStack is used as in the TS. Plain TS `Error` throws print
 * "Error: <message>" and abort.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_CVARS_MIRROR_H
#define QA_CORE_CVARS_MIRROR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_cvar_mirror_read {
  const char *reset_value; /* borrowed from the registry */
  int64_t flags;
} qa_cvar_mirror_read;

typedef struct qa_cvar_mirror_binding {
  void *ctx;
  /* Return an explanation to reject the value, or NULL. */
  const char *(*validate)(void *ctx, const char *value);
  void (*changed)(void *ctx, const char *value);
} qa_cvar_mirror_binding;

typedef struct qa_cvar_mirror_release {
  void *ctx;
  void (*release)(void *ctx);
} qa_cvar_mirror_release;

typedef struct qa_cvar_mirror_registry {
  void *ctx;
  const void *session;
  const char *dialect;
  bool (*find)(void *ctx, const char *name, qa_cvar_mirror_read *out);
  void (*register_cvar)(void *ctx, const char *name,
                        const char *default_value, int64_t flags);
  void (*set)(void *ctx, const char *name, const char *value, bool force);
  /* Borrowed current value text, "" when unregistered. */
  const char *(*variable_string)(void *ctx, const char *name);
  qa_cvar_mirror_release (*bind_value)(void *ctx, const char *name,
                                       qa_cvar_mirror_binding binding);
} qa_cvar_mirror_registry;

typedef void (*qa_cvar_mirror_assert_fn)(void *ctx);

typedef struct qa_shared_cvar_mirror qa_shared_cvar_mirror; /* opaque */

/* Returns NULL only on allocation failure (after closing anything it
 * had already bound); the TS constructor's Error throws abort.
 * `assert_current` may be NULL for a no-op (the TS requires the
 * closure; tests pass an empty one). Registries are borrowed. */
qa_shared_cvar_mirror *qa_shared_cvar_mirror_create(
    const qa_cvar_mirror_registry *owner,
    const qa_cvar_mirror_registry *mirror, const char *const *names,
    size_t name_count, qa_cvar_mirror_assert_fn assert_current,
    void *assert_ctx);
void qa_shared_cvar_mirror_refresh(qa_shared_cvar_mirror *mirror);
void qa_shared_cvar_mirror_close(qa_shared_cvar_mirror *mirror);
/* close plus freeing the mirror itself. */
void qa_shared_cvar_mirror_destroy(qa_shared_cvar_mirror *mirror);

/* A CvarRegistry as the registry surface above (TS passes the registry
 * itself). ctx is the registry, so shared subscriptions key on registry
 * identity; session is the registry context's session token and dialect
 * its TS dialect text. Borrowed strings stay valid until the next find or
 * variable_string through any registry view. */
struct qa_cvar_registry;
qa_cvar_mirror_registry qa_cvar_registry_mirror_view(struct qa_cvar_registry *registry);

#endif
