/*
 * Port of src/world/gameplay/mod-composition.ts: ordered mod
 * transform/observe/replace composition around a canonical operation.
 * TS generics become request/result element sizes with memcpy storage;
 * the unregister closure becomes a handle + qa_mod_operation_unregister.
 * TS Error throws abort with the identical message (see .c).
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_GAMEPLAY_MOD_COMPOSITION_H
#define QA_WORLD_GAMEPLAY_MOD_COMPOSITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"

/** TS ModRegistrationIdentity. */
typedef struct qa_mod_registration_identity {
  qa_provider_id provider;
  const char *id; /* `${string}:${string}` */
  int64_t order;
} qa_mod_registration_identity;

/** TS next continuation handed to a replace contribution. */
typedef struct qa_mod_next {
  void *ctx;
  void (*call)(void *ctx, const void *request, void *out_result);
} qa_mod_next;

/* TS ModOperationRegistration<Request, Result> on kind. */
typedef enum qa_mod_registration_kind {
  QA_MOD_TRANSFORM, QA_MOD_OBSERVE, QA_MOD_REPLACE
} qa_mod_registration_kind;
typedef struct qa_mod_operation_registration {
  qa_mod_registration_identity identity;
  qa_mod_registration_kind kind;
  void *ctx;
  void (*transform)(void *ctx, const void *request, void *out_request);
  void (*observe)(void *ctx, const void *request, const void *result);
  void (*replace)(void *ctx, const void *request, qa_mod_next next, void *out_result);
} qa_mod_operation_registration;

typedef struct qa_mod_operation_entry qa_mod_operation_entry;

/** TS ModOperation<Request, Result>. */
typedef struct qa_mod_operation {
  const char *name;
  size_t request_size;
  size_t result_size;
  qa_mod_operation_entry *entries;
  size_t entry_count;
  size_t entry_capacity;
  int64_t sequence;
} qa_mod_operation;

/* TS canonical / beforeObservers closures: fn ptr + ctx. */
typedef struct qa_mod_canonical {
  void *ctx;
  void (*call)(void *ctx, const void *request, void *out_result);
} qa_mod_canonical;
typedef struct qa_mod_before_observers {
  void *ctx;
  void (*call)(void *ctx);
} qa_mod_before_observers;

/* Returns false only on allocation failure. */
bool qa_mod_operation_init(qa_mod_operation *operation, const char *name,
                           size_t request_size, size_t result_size);
void qa_mod_operation_destroy(qa_mod_operation *operation);
bool qa_mod_operation_active(const qa_mod_operation *operation);
/* Registers; returns the entry sequence (the TS unregister closure's
 * identity), or -1 on allocation failure. Invalid order / duplicates /
 * conflicting replacements abort with the TS messages. */
int64_t qa_mod_operation_register(qa_mod_operation *operation,
                                  qa_mod_operation_registration contribution);
void qa_mod_operation_unregister(qa_mod_operation *operation, int64_t sequence);
/* before_observers may be NULL (TS optional). */
void qa_mod_operation_dispatch(qa_mod_operation *operation, const void *input,
                               qa_mod_canonical canonical,
                               const qa_mod_before_observers *before_observers,
                               void *out_result);
void qa_mod_operation_clear(qa_mod_operation *operation);

#endif
