/*
 * Port of src/world/session/mod-objectives.ts: borrowed objective
 * words projected between an original owner and destination match
 * services. The generic <Scalar, Reference, Call> payloads stay
 * opaque pointers (contracts declaration shape). TS plain Error
 * throws abort identically; the activate() rollback of partially
 * bound objectives cannot run in C (the failing call aborts).
 * The TS ReadonlyMap of callback inputs becomes a fixed entry array.
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_MOD_OBJECTIVES_H
#define QA_WORLD_SESSION_MOD_OBJECTIVES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/source_match.h"

typedef struct qa_mod_objective_input_entry {
  qa_mod_callback_input name;
  qa_mod_runtime_value value;
} qa_mod_objective_input_entry;

/** TS Storage<Scalar, Reference, Call>. read_actor false is the TS
 * null; write_actor takes a nullable actor. location returns a
 * borrowed string compared by value. */
typedef struct qa_mod_objective_storage {
  void *ctx;
  bool (*current)(void *ctx);
  const char *(*location)(void *ctx,
                          const qa_source_objective_declaration *declaration);
  double (*read_scalar)(void *ctx, const void *storage);
  void (*write_scalar)(void *ctx, const void *storage, double value);
  bool (*read_actor)(void *ctx, const void *storage, qa_actor_id *out);
  void (*write_actor)(void *ctx, const void *storage,
                      const qa_actor_id *value);
  void (*invoke)(void *ctx, const void *call,
                 const qa_mod_objective_input_entry *inputs, size_t count);
  double (*seconds)(void *ctx);
} qa_mod_objective_storage;

typedef struct qa_mod_source_objectives qa_mod_source_objectives;

/* match may be NULL only with no declarations. NULL only on OOM. */
qa_mod_source_objectives *qa_mod_source_objectives_create(
    qa_provider_id owner,
    const qa_source_objective_declaration *declarations, size_t count,
    const qa_source_match_services *match,
    qa_mod_objective_storage storage);
void qa_mod_source_objectives_destroy(qa_mod_source_objectives *objectives);
void qa_mod_source_objectives_activate(qa_mod_source_objectives *objectives);
void qa_mod_source_objectives_refresh(qa_mod_source_objectives *objectives);
void qa_mod_source_objectives_flush(qa_mod_source_objectives *objectives);
void qa_mod_source_objectives_restored(qa_mod_source_objectives *objectives);
void qa_mod_source_objectives_close(qa_mod_source_objectives *objectives);

#endif
