/* Port of src/compat/qc/borrowed-actors.ts. GPL-2.0-or-later.
 * Host combat read is mirrored (TS GameplayAuthority.read,
 * src/world/gameplay/authority.ts): health/canTakeDamage only.
 * Checkpoint rows are caller-freed; restore consumes a SaveReader. */
#ifndef QA_COMPAT_QC_BORROWED_ACTORS_H
#define QA_COMPAT_QC_BORROWED_ACTORS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/machine.h"
#include "qa/contracts/session.h"
#include "qa/contracts/world.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/registry.h"
#include "qa/world/actors/source_slots.h"
typedef struct qa_compat_qc_borrowed_host {
  qa_session_actor_registry *actors; qa_body_table bodies;
  qa_source_actor_slots *slots; qa_source_actor_slots_options slots_options;
  void *ctx;
  qa_compat_qc_machine *(*machine)(void *ctx);
  /* Mirrored GameplayAuthority.read subset. False = TS null. */
  bool (*combat_read)(void *ctx, qa_actor_id actor, double *out_health, bool *out_can_take_damage);
  const char *(*classname)(void *ctx, qa_actor_id actor);
  int64_t (*solid)(void *ctx, qa_actor_id actor); /* 0..4 */
} qa_compat_qc_borrowed_host;
typedef struct qa_compat_qc_borrowed_row { qa_owned_actor actor; int64_t slot; char *classname; } qa_compat_qc_borrowed_row;
typedef struct qa_compat_qc_borrowed_actors {
  qa_compat_qc_borrowed_host host;
  qa_compat_qc_borrowed_row *rows; size_t row_count, row_capacity;
  bool offsets_ready; int64_t offsets[14]; int64_t *fields_by_word; size_t fields_by_word_count;
  int64_t release_id;
} qa_compat_qc_borrowed_actors;
void qa_compat_qc_borrowed_actors_init(qa_compat_qc_borrowed_actors *b, qa_compat_qc_borrowed_host host);
void qa_compat_qc_borrowed_actors_destroy(qa_compat_qc_borrowed_actors *b);
bool qa_compat_qc_borrowed_actors_actor(qa_compat_qc_borrowed_actors *b, int64_t slot, qa_owned_actor *out);
int64_t qa_compat_qc_borrowed_actors_reference(qa_compat_qc_borrowed_actors *b, qa_actor_id actor);
/* kind "read"|"write"; words 1|3 */
void qa_compat_qc_borrowed_actors_access(qa_compat_qc_borrowed_actors *b, int64_t reference, int64_t word, int64_t words, const char *kind);
/* TS observeStore: a QC velocity store into a borrowed row writes the
 * canonical body velocity. after holds the stored words (little endian). */
void qa_compat_qc_borrowed_actors_observe_store(qa_compat_qc_borrowed_actors *b, int64_t reference, int64_t word,
    const uint8_t *after, size_t after_length);
typedef struct qa_compat_qc_borrowed_checkpoint { qa_saved_actor_id actor; int64_t slot; } qa_compat_qc_borrowed_checkpoint;
size_t qa_compat_qc_borrowed_actors_checkpoint(qa_compat_qc_borrowed_actors *b, qa_compat_qc_borrowed_checkpoint *out, size_t capacity);
void qa_compat_qc_borrowed_actors_restore(qa_compat_qc_borrowed_actors *b, const qa_save_reader *reader);
#endif
