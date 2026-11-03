/* Port of src/core/cvars/mirror.ts. GPL-2.0-or-later. */
#include "qa/core/cvars/mirror.h"

#include "qa/foundation/support.h"
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/cvars/index.h"
#include "qa/foundation/cleanup.h"

/* Plain TS `Error` throws: catchable raises named "Error". */
static noreturn void throw_error(const char *message) {
  qa_raise("Error", 0, 0, "%s", message);
}
static noreturn void throw_error_name(const char *prefix, const char *name,
                                      const char *suffix) {
  qa_raise("Error", 0, 0, "%s%s%s", prefix, name, suffix);
}

struct mirror_binding_ctx {
  struct qa_shared_cvar_mirror *self;
  const char *name; /* borrowed from self->names */
};

struct qa_shared_cvar_mirror {
  const qa_cvar_mirror_registry *owner;
  const qa_cvar_mirror_registry *mirror;
  char **names;
  size_t name_count;
  struct mirror_binding_ctx *binding_ctxs; /* one per name */
  qa_cvar_mirror_assert_fn assert_current;
  void *assert_ctx;
  qa_cleanup_stack releases;
  bool refreshing;
};

/* One shared owner subscription per (owner, name): the TS WeakMap
 * entry. Allocated individually so binding ctx pointers stay put. */
struct subscription {
  const void *owner_ctx;
  char *name;
  qa_shared_cvar_mirror **clients;
  size_t client_count;
  size_t client_capacity;
  qa_cvar_mirror_release release;
};

static struct subscription **subscriptions = NULL;
static size_t subscription_count = 0;
static size_t subscription_capacity = 0;

static char *copy_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

static const char *validate_ok(void *ctx, const char *value) {
  (void)ctx;
  (void)value;
  return NULL;
}

static void mirror_changed(void *ctx, const char *value) {
  struct mirror_binding_ctx *binding = (struct mirror_binding_ctx *)ctx;
  qa_shared_cvar_mirror *self = binding->self;
  if (!self->refreshing) {
    if (self->assert_current != NULL) {
      self->assert_current(self->assert_ctx);
    }
    self->owner->set(self->owner->ctx, binding->name, value, true);
  }
}

static void owner_changed(void *ctx, const char *value) {
  (void)value;
  struct subscription *subscription = (struct subscription *)ctx;
  for (size_t i = 0; i < subscription->client_count; i++) {
    qa_shared_cvar_mirror_refresh(subscription->clients[i]);
  }
}

static void run_release(void *ctx) {
  qa_cvar_mirror_release *release = (qa_cvar_mirror_release *)ctx;
  release->release(release->ctx);
  free(release);
}

static struct subscription *find_subscription(const void *owner_ctx,
                                              const char *name) {
  for (size_t i = 0; i < subscription_count; i++) {
    if (subscriptions[i]->owner_ctx == owner_ctx &&
        strcmp(subscriptions[i]->name, name) == 0) {
      return subscriptions[i];
    }
  }
  return NULL;
}

static void remove_subscription(struct subscription *subscription) {
  for (size_t i = 0; i < subscription_count; i++) {
    if (subscriptions[i] == subscription) {
      for (size_t j = i + 1; j < subscription_count; j++) {
        subscriptions[j - 1] = subscriptions[j];
      }
      subscription_count--;
      break;
    }
  }
  free(subscription->name);
  free(subscription->clients);
  free(subscription);
}

qa_shared_cvar_mirror *qa_shared_cvar_mirror_create(
    const qa_cvar_mirror_registry *owner,
    const qa_cvar_mirror_registry *mirror, const char *const *names,
    size_t name_count, qa_cvar_mirror_assert_fn assert_current,
    void *assert_ctx) {
  if (owner == mirror || owner->ctx == mirror->ctx ||
      owner->session != mirror->session ||
      strcmp(owner->dialect, mirror->dialect) != 0) {
    throw_error(
        "Shared cvar mirror requires distinct registries in the same "
        "session and dialect");
  }
  qa_shared_cvar_mirror *self =
      (qa_shared_cvar_mirror *)calloc(1, sizeof(qa_shared_cvar_mirror));
  if (self == NULL) return NULL;
  self->owner = owner;
  self->mirror = mirror;
  self->assert_current = assert_current;
  self->assert_ctx = assert_ctx;
  qa_cleanup_stack_init(&self->releases);
  if (name_count > 0) {
    self->names = (char **)calloc(name_count, sizeof(char *));
    if (self->names == NULL) {
      qa_shared_cvar_mirror_destroy(self);
      return NULL;
    }
    self->name_count = name_count;
    for (size_t i = 0; i < name_count; i++) {
      self->names[i] = copy_text(names[i]);
      if (self->names[i] == NULL) {
        qa_shared_cvar_mirror_destroy(self);
        return NULL;
      }
    }
  }
  for (size_t i = 0; i < self->name_count; i++) {
    qa_cvar_mirror_read value;
    if (!owner->find(owner->ctx, self->names[i], &value)) {
      char *name = self->names[i];
      qa_shared_cvar_mirror_destroy(self);
      /* `name` is freed by destroy; report the original pointer text. */
      (void)name;
      throw_error_name("Shared engine cvar ", names[i], " is not declared");
    }
    mirror->register_cvar(mirror->ctx, self->names[i], value.reset_value,
                          value.flags);
  }
  qa_shared_cvar_mirror_refresh(self);
  if (self->name_count > 0) {
    self->binding_ctxs = (struct mirror_binding_ctx *)calloc(
        self->name_count, sizeof(struct mirror_binding_ctx));
    if (self->binding_ctxs == NULL) {
      qa_shared_cvar_mirror_destroy(self);
      return NULL;
    }
  }
  for (size_t i = 0; i < self->name_count; i++) {
    const char *name = self->names[i];
    qa_cvar_mirror_release *release_copy =
        (qa_cvar_mirror_release *)malloc(sizeof(qa_cvar_mirror_release));
    if (release_copy == NULL) {
      qa_shared_cvar_mirror_destroy(self);
      return NULL;
    }
    self->binding_ctxs[i].self = self;
    self->binding_ctxs[i].name = name;
    qa_cvar_mirror_binding binding = {&self->binding_ctxs[i], validate_ok,
                                      mirror_changed};
    *release_copy = mirror->bind_value(mirror->ctx, name, binding);
    if (qa_cleanup_stack_add(&self->releases, run_release, release_copy) != 0) {
      release_copy->release(release_copy->ctx);
      free(release_copy);
      qa_shared_cvar_mirror_destroy(self);
      return NULL;
    }
    struct subscription *subscription = find_subscription(owner->ctx, name);
    if (subscription == NULL) {
      subscription =
          (struct subscription *)calloc(1, sizeof(struct subscription));
      if (subscription == NULL) {
        qa_shared_cvar_mirror_destroy(self);
        return NULL;
      }
      subscription->owner_ctx = owner->ctx;
      subscription->name = copy_text(name);
      if (subscription->name == NULL) {
        free(subscription);
        qa_shared_cvar_mirror_destroy(self);
        return NULL;
      }
      qa_cvar_mirror_binding owner_binding = {subscription, validate_ok,
                                              owner_changed};
      subscription->release = owner->bind_value(owner->ctx, name, owner_binding);
      if (subscription_count == subscription_capacity) {
        size_t grown =
            subscription_capacity == 0 ? 8 : subscription_capacity * 2;
        struct subscription **grown_list = (struct subscription **)realloc(
            subscriptions, grown * sizeof(struct subscription *));
        if (grown_list == NULL) {
          subscription->release.release(subscription->release.ctx);
          free(subscription->name);
          free(subscription);
          qa_shared_cvar_mirror_destroy(self);
          return NULL;
        }
        subscriptions = grown_list;
        subscription_capacity = grown;
      }
      subscriptions[subscription_count++] = subscription;
    }
    if (subscription->client_count == subscription->client_capacity) {
      size_t grown =
          subscription->client_capacity == 0 ? 4
                                             : subscription->client_capacity * 2;
      qa_shared_cvar_mirror **clients = (qa_shared_cvar_mirror **)realloc(
          subscription->clients, grown * sizeof(qa_shared_cvar_mirror *));
      if (clients == NULL) {
        qa_shared_cvar_mirror_destroy(self);
        return NULL;
      }
      subscription->clients = clients;
      subscription->client_capacity = grown;
    }
    subscription->clients[subscription->client_count++] = self;
  }
  return self;
}

static void refreshing_release(void *raw) { ((qa_shared_cvar_mirror *)raw)->refreshing = false; }

void qa_shared_cvar_mirror_refresh(qa_shared_cvar_mirror *mirror) {
  mirror->refreshing = true;
  /* TS try { ... } finally { this.refreshing = false; } */
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, refreshing_release, mirror);
  for (size_t i = 0; i < mirror->name_count; i++) {
    const char *value =
        mirror->owner->variable_string(mirror->owner->ctx, mirror->names[i]);
    mirror->mirror->set(mirror->mirror->ctx, mirror->names[i], value, true);
  }
  qa_cleanup_pop(&cleanup);
  mirror->refreshing = false;
}

void qa_shared_cvar_mirror_close(qa_shared_cvar_mirror *mirror) {
  qa_cleanup_stack_run_all(&mirror->releases);
  for (size_t i = 0; i < mirror->name_count; i++) {
    if (mirror->names[i] == NULL) continue; /* partial create rollback */
    struct subscription *subscription =
        find_subscription(mirror->owner->ctx, mirror->names[i]);
    if (subscription == NULL) continue;
    for (size_t j = 0; j < subscription->client_count; j++) {
      if (subscription->clients[j] == mirror) {
        for (size_t k = j + 1; k < subscription->client_count; k++) {
          subscription->clients[k - 1] = subscription->clients[k];
        }
        subscription->client_count--;
        break;
      }
    }
    if (subscription->client_count == 0) {
      subscription->release.release(subscription->release.ctx);
      remove_subscription(subscription);
    }
  }
}

void qa_shared_cvar_mirror_destroy(qa_shared_cvar_mirror *mirror) {
  if (mirror == NULL) return;
  qa_shared_cvar_mirror_close(mirror);
  for (size_t i = 0; i < mirror->name_count; i++) free(mirror->names[i]);
  free(mirror->names);
  free(mirror->binding_ctxs);
  qa_cleanup_stack_destroy(&mirror->releases);
  free(mirror);
}

/* ---- CvarRegistry view ---- */
static char *view_scratch;
static char *view_hold(char *text) {
  free(view_scratch);
  view_scratch = text;
  return text;
}
static bool view_find(void *ctx, const char *name, qa_cvar_mirror_read *out) {
  qa_cvar_read read;
  if (!qa_cvar_registry_find(ctx, name, &read)) return false;
  out->reset_value = view_hold(strdup(read.reset_value != NULL ? read.reset_value : ""));
  out->flags = read.flags;
  qa_cvar_read_free(&read);
  if (out->reset_value == NULL) throw_error_name("Shared engine cvar ", name, " could not be copied");
  return true;
}
static void view_register(void *ctx, const char *name, const char *default_value, int64_t flags) {
  qa_cvar_snapshot ignored;
  if (qa_cvar_registry_register(ctx, name, default_value, (double)flags, &ignored)) qa_cvar_read_free(&ignored);
}
static void view_set(void *ctx, const char *name, const char *value, bool force) {
  qa_cvar_snapshot ignored;
  if (qa_cvar_registry_set(ctx, name, value, force, &ignored)) qa_cvar_read_free(&ignored);
}
static const char *view_variable_string(void *ctx, const char *name) {
  char *value = qa_cvar_registry_variable_string(ctx, name);
  if (value == NULL) throw_error_name("Shared engine cvar ", name, " could not be read");
  return view_hold(value);
}
typedef struct view_binding {
  qa_cvar_registry *registry;
  int64_t handle;
  qa_cvar_mirror_binding binding;
} view_binding;
static char *view_validate(void *ctx, const char *value) {
  view_binding *binding = ctx;
  const char *explanation = binding->binding.validate(binding->binding.ctx, value);
  return explanation == NULL ? NULL : strdup(explanation);
}
static void view_changed(void *ctx, const char *value) {
  view_binding *binding = ctx;
  binding->binding.changed(binding->binding.ctx, value);
}
static void view_release(void *ctx) {
  view_binding *binding = ctx;
  qa_cvar_registry_unbind_value(binding->registry, binding->handle);
  free(binding);
}
static qa_cvar_mirror_release view_bind_value(void *ctx, const char *name, qa_cvar_mirror_binding binding) {
  view_binding *bound = calloc(1, sizeof *bound);
  if (bound == NULL) throw_error_name("Shared engine cvar ", name, " could not be bound");
  bound->registry = ctx;
  bound->binding = binding;
  qa_cvar_value_binding value = {bound, binding.validate != NULL ? view_validate : NULL, view_changed};
  bound->handle = qa_cvar_registry_bind_value(ctx, name, value);
  qa_cvar_mirror_release release = {bound, view_release};
  return release;
}
qa_cvar_mirror_registry qa_cvar_registry_mirror_view(struct qa_cvar_registry *registry) {
  static const char *const dialects[] = {"q1-netquake", "q1-quakeworld", "q2-classic", "q2-rerelease", "q3"};
  qa_cvar_mirror_registry view;
  view.ctx = registry;
  view.session = qa_cvar_registry_context(registry).session.token;
  view.dialect = dialects[qa_cvar_registry_dialect(registry)];
  view.find = view_find;
  view.register_cvar = view_register;
  view.set = view_set;
  view.variable_string = view_variable_string;
  view.bind_value = view_bind_value;
  return view;
}
