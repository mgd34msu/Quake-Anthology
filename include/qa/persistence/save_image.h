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
 * encodeCheckpointValue serializes the plain object); two nested
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
