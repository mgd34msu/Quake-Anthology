/*
 * Port of src/persistence/save-image.ts: the unified QTSAVE save
 * image codec and its boundary readers. Ownership: returned
 * structs borrow strings from the checkpoint tree; pointer fields
 * (ground actor) are single malloc'd allocations the caller frees.
 *
 * qa_parse_save_image borrows the caller's checkpoint tree (the
 * result's root stays NULL); qa_decode_save_image /
 * qa_read_save_image own the decoded tree in root. Release either
 * with qa_save_image_result_free, which frees the list arrays and
 * single allocations made by save_image.c itself plus an owned
 * root; recipe/mods interiors follow their readers' convention
 * (caller frees their list arrays and built identity strings).
 * Encode builds the checkpoint tree from the struct (TS
 * encodeCheckpointValue serializes the plain object, in the key order
 * image.key_order names) and writes the QTSAVE5 envelope with its
 * header; decode reads QTSAVE5/4 (header) and QTSAVE3/2. Two nested
 * writers do not exist yet and abort with a C-only
 * SaveFormatError when present, per the saved_game.h precedent:
 * a recipe gameplay mod's declaration, and a q3-qvm grapple
 * profile. The source actor checkpoint helpers below: capture
 * returns false only on OOM and owns its bytes (caller frees
 * bytes.bytes); read results borrow provider strings from `root`
 * (release with qa_source_actors_result_free).
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_SAVE_IMAGE_H
#define QA_PERSISTENCE_SAVE_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/execution.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/mods.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/registry.h"

qa_saved_actor_id qa_saved_actor_id_of(qa_actor_id actor);
qa_saved_actor_id qa_read_saved_actor(const qa_save_reader *reader);
/* The saved form of a GuestCheckpoint (owned tree; NULL only on OOM). */
qa_save_value *qa_save_guest_checkpoint_value(const qa_guest_checkpoint *guest);
/* The saved form of a ModIdentity (owned tree; NULL only on OOM). */
qa_save_value *qa_save_mod_identity_value(const qa_mod_identity *identity);
/* The value of a ResolvedResourceReference as mounts/index.ts records it ({ ...resource, id }). NULL only on OOM. */
qa_save_value *qa_save_resource_reference_value(const qa_resolved_resource_reference *resource);
qa_saved_body_state qa_read_saved_body(const qa_save_reader *reader);
qa_saved_body_attachment qa_read_saved_body_attachment(
    const qa_save_reader *reader);
qa_regular_armor_state qa_read_regular_armor(const qa_save_reader *reader);
qa_armor_state qa_read_armor(const qa_save_reader *reader);
qa_inventory_entry qa_read_inventory_entry(const qa_save_reader *reader);

/* Parsed image plus the checkpoint tree its strings borrow from
 * (owned by decode/read, NULL for a caller-supplied parse tree). */
typedef struct qa_save_image_result {
  qa_save_image image;
  qa_save_value *root; /* owned or NULL */
} qa_save_image_result;
/* TS parseSaveImage. False only on OOM; malformed input aborts as
 * the TS SaveFormatError throws. */
bool qa_parse_save_image(const qa_save_value *value, qa_save_image_result *out);
/* TS encodeSaveImage. Malloc'd bytes in *out_bytes; false only on OOM. */
bool qa_encode_save_image(const qa_save_image *image, uint8_t **out_bytes,
                          size_t *out_length);
/* TS decodeSaveImage. False only on OOM. */
bool qa_decode_save_image(const uint8_t *bytes, size_t length,
                          qa_save_image_result *out);
/* TS readSaveImage / writeSaveImage (synchronous). False on I/O
 * failure or OOM; write commits by rename of a completed sibling. */
bool qa_read_save_image(const char *path, qa_save_image_result *out);
bool qa_write_save_image(const char *path, const qa_save_image *image);
void qa_save_image_result_free(qa_save_image_result *result);

/* TS SaveHeader: menu metadata stored ahead of the payload (QTSAVE5 and
 * QTSAVE4) so listing a save never decodes guest memory. Strings borrow
 * from the result's root. */
typedef struct qa_save_header {
  const char *map;
  qa_content_id content;
} qa_save_header;
typedef struct qa_save_header_result {
  bool present; /* false = TS null (an older envelope carries no header) */
  qa_save_header header;
  qa_save_value *root; /* owned */
} qa_save_header_result;
/* TS readSaveHeader (synchronous). False on I/O failure or OOM; a
 * malformed header aborts as the TS SaveFormatError throws. */
bool qa_read_save_header(const char *path, qa_save_header_result *out);
void qa_save_header_result_free(qa_save_header_result *result);

/* Value builders for the TS plain records the save image embeds
 * (encodeCheckpointValue key order); NULL only on OOM. */
qa_save_value *qa_save_actor_value(qa_saved_actor_id actor);
qa_save_value *qa_save_vector_value(qa_vec3 vector);
qa_save_value *qa_save_bounds_value(qa_bounds bounds);
qa_save_value *qa_save_time_value(qa_source_time time);
qa_save_value *qa_save_frame_value(qa_frame_context frame);
qa_save_value *qa_save_random_value(qa_random_state state);
qa_save_value *qa_save_provider_value(qa_provider_reference provider);
qa_save_value *qa_save_armor_value(qa_armor_state armor);
qa_save_value *qa_save_combat_value(qa_combat_state state);
qa_save_value *qa_save_inventory_entry_value(qa_inventory_entry entry);
qa_save_value *qa_save_recipe_value(const qa_executable_recipe *recipe);
qa_save_value *qa_save_guest_value(const qa_guest_checkpoint *guest);
/* WeaponBehaviorDefinition in its recipe key order (owned tree; NULL only on OOM). */
qa_save_value *qa_save_weapon_behavior_definition_value(const qa_weapon_behavior_definition *definition);

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
