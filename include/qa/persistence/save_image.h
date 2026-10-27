/*
 * Port of src/persistence/save-image.ts: the unified QTSAVE save
 * image codec and its boundary readers. Ownership: returned
 * structs borrow strings from the checkpoint tree; pointer fields
 * (ground actor) are single malloc'd allocations the caller frees.
 *
 * BLOCKED: qa_parse_save_image / qa_encode_save_image /
 * qa_decode_save_image / file read+write are not yet ported. Parse
 * and decode need qa_read_recipe (see recipe.h: persistence/mods.h
 * still declares no qa_read_gameplay_mod); readModSession exists
 * (qa_read_mod_session in mods.h). Encode additionally needs
 * checkpoint-value writers for the recipe and its nested content
 * types, which do not exist yet. The exported readers and the
 * source actor checkpoint helpers below are complete: capture
 * returns false only on OOM and owns its bytes (caller frees
 * bytes.bytes); read results borrow provider strings from `root`
 * (release with qa_source_actors_result_free).
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_SAVE_IMAGE_H
#define QA_PERSISTENCE_SAVE_IMAGE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/registry.h"

qa_saved_actor_id qa_saved_actor_id_of(qa_actor_id actor);
qa_saved_actor_id qa_read_saved_actor(const qa_save_reader *reader);
qa_saved_body_state qa_read_saved_body(const qa_save_reader *reader);
qa_saved_body_attachment qa_read_saved_body_attachment(
    const qa_save_reader *reader);
qa_regular_armor_state qa_read_regular_armor(const qa_save_reader *reader);
qa_armor_state qa_read_armor(const qa_save_reader *reader);
qa_inventory_entry qa_read_inventory_entry(const qa_save_reader *reader);

/* TS sourceActorsCheckpoint: the "world:source-slots" provider
 * checkpoint over a SourceActorCheckpoint list. */
bool qa_source_actors_checkpoint(const qa_source_actor_checkpoint *sources,
                                 size_t count, qa_provider_checkpoint *out);
typedef struct qa_source_actors_result {
  qa_source_actor_checkpoint *entries; /* owned array */
  size_t count;
  qa_save_value *root; /* owned */
} qa_source_actors_result;
/* TS readSourceActorsCheckpoint. False only on OOM; an unsupported
 * checkpoint identity aborts as the TS SaveFormatError throws. */
bool qa_read_source_actors_checkpoint(const qa_provider_checkpoint *checkpoint,
                                      qa_source_actors_result *out);
void qa_source_actors_result_free(qa_source_actors_result *result);

#endif
