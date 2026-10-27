/*
 * Port of src/content/composition/q2/save.ts: capture/restore of the
 * Q2 source-private checkpoint records. Per-module capture/restore plus
 * the persistence codecs (landed under qa/persistence/q2_*.h at the
 * hook side) are mirrored as decode-then-restore hooks so the TS
 * two-phase ordering (all decodes before any restore) is kept.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q2_SAVE_H
#define QA_CONTENT_COMPOSITION_Q2_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/composition/q2/index.h"
#include "qa/contracts/session.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/value.h"

/* Schema keys whose payloads flow through the generic module hooks. */
typedef enum qa_comp_q2_save_module {
  QA_COMP_Q2_SAVE_FOUNDATION, QA_COMP_Q2_SAVE_ITEMS, QA_COMP_Q2_SAVE_MOVERS, QA_COMP_Q2_SAVE_MONSTERS,
  QA_COMP_Q2_SAVE_WEAPONS, QA_COMP_Q2_SAVE_PLAYERS, QA_COMP_Q2_SAVE_BASE_ENTITIES,
  QA_COMP_Q2_SAVE_MISSIONPACK_ITEMS, QA_COMP_Q2_SAVE_PACK_MONSTERS,
  QA_COMP_Q2_SAVE_RERELEASE_PLAYERS, QA_COMP_Q2_SAVE_RERELEASE_ENTITIES
} qa_comp_q2_save_module;

/* Match-mode checkpoint states save.ts extracts with the SaveReader. */
typedef struct qa_comp_q2_tag_state {
  bool has_token;
  qa_saved_actor_id token;
  bool has_owner;
  qa_saved_actor_id owner;
  double count;
} qa_comp_q2_tag_state;
typedef struct qa_comp_q2_ball_state {
  bool has_ball;
  qa_saved_actor_id ball;
  double starts;
  double team1_score;
  double team2_score;
} qa_comp_q2_ball_state;
typedef struct qa_comp_q2_horde_state {
  double wave;
  double spawned;
  double intermission;
  const qa_saved_actor_id *monsters;
  size_t monster_count;
} qa_comp_q2_horde_state;

typedef struct qa_comp_q2_save_hooks {
  void *ctx;
  qa_provider_id provider; /* game.options.provider */
  double (*deathmatch_flags)(void *ctx, qa_comp_q2_product_runtime *source);
  /* Module capture -> encoded bytes (malloc'd; NULL is OOM). pack is
   * set only for QA_COMP_Q2_SAVE_PACK_MONSTERS. */
  uint8_t *(*capture_module)(void *ctx, qa_comp_q2_product_runtime *source, qa_comp_q2_save_module module,
                             const char *pack, size_t *out_length);
  /* Phase 1: decode bytes into an owned handle (NULL is OOM). */
  void *(*decode_module)(void *ctx, qa_comp_q2_save_module module, const char *pack,
                         const uint8_t *bytes, size_t length);
  /* Phase 2: restore from the decoded handle, then free it. */
  void (*restore_module)(void *ctx, qa_comp_q2_product_runtime *source, qa_comp_q2_save_module module,
                         const char *pack, void *decoded);
  void (*free_decoded)(void *ctx, void *decoded);
  /* match.source capture/restore. capture returns an owned value tree. */
  qa_save_value *(*capture_match)(void *ctx, qa_comp_q2_product_runtime *source);
  void (*restore_ctf)(void *ctx, qa_comp_q2_product_runtime *source, const uint8_t *bytes, size_t length);
  void (*restore_lmctf)(void *ctx, qa_comp_q2_product_runtime *source, qa_save_reader *reader);
  void (*restore_tag)(void *ctx, qa_comp_q2_product_runtime *source, const qa_comp_q2_tag_state *state);
  void (*restore_ball)(void *ctx, qa_comp_q2_product_runtime *source, const qa_comp_q2_ball_state *state);
  void (*restore_horde)(void *ctx, qa_comp_q2_product_runtime *source, const qa_comp_q2_horde_state *state);
} qa_comp_q2_save_hooks;

/**
 * TS captureQ2Product(). Writes up to capacity records; each record's
 * bytes are malloc'd (release with qa_comp_q2_save_free_records).
 * Returns the record count, or 0 on OOM / insufficient capacity.
 */
size_t qa_comp_capture_q2_product(qa_comp_q2_product_runtime *source, const qa_comp_q2_save_hooks *hooks,
                                  qa_provider_checkpoint *out, size_t capacity);
void qa_comp_q2_save_free_records(qa_provider_checkpoint *records, size_t count);

/** TS restoreQ2Product(). False is OOM; checkpoint errors abort. */
bool qa_comp_restore_q2_product(qa_comp_q2_product_runtime *source, const qa_comp_q2_save_hooks *hooks,
                                const qa_provider_checkpoint *checkpoints, size_t checkpoint_count);

#endif
