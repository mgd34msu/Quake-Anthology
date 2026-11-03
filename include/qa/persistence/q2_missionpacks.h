/*
 * Port of src/persistence/q2-missionpacks.ts. GPL-2.0-or-later.
 * The codecs operate on the content checkpoint types, as in TS (one C
 * type per TS type):
 *   Q2MissionPackItemsCheckpoint    qa_q2mp_items_checkpoint     (missionpacks/items.h)
 *   Q2MissionPackMonstersCheckpoint qa_q2mpm_monsters_checkpoint (missionpacks/monsters/state.h)
 *   Q2RogueEntitiesCheckpoint       qa_q2mp_rogue_entities_checkpoint (missionpacks/entities/rogue.h)
 *   Q2TagCheckpoint                 qa_q2mp_tag_checkpoint       (missionpacks/modes/tag.h)
 *   Q2DeathBallCheckpoint           qa_q2mp_deathball_checkpoint (missionpacks/modes/deathball.h)
 * Hints decode through the content port of decodeQ2RogueHintsCheckpoint
 * (qa_q2mpm_decode_hints_checkpoint).
 *
 * Encoders return malloc'd bytes (NULL only on OOM). TS encodes the
 * checkpoint object as captured (encodeCheckpointValue keeps key insertion
 * order), so the C encoders write each object in the content capture's key
 * order; for monsters that is version, actors, flyerNextMove, hints,
 * widowShotsFired, widowDamageMultiplier.
 *
 * Readers and decoders fill *out with owned values: release items with
 * qa_q2mp_items_checkpoint_free and monsters with
 * qa_q2mpm_monsters_checkpoint_free; the rogue-entities, tag and deathball
 * checkpoints own no memory. They return false for OOM (and decoders for
 * bytes that are not a checkpoint value); malformed fields fail through
 * the save reader, as the TS SaveReader throws.
 */
#ifndef QA_PERSISTENCE_Q2_MISSIONPACKS_H
#define QA_PERSISTENCE_Q2_MISSIONPACKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q2/missionpacks/entities/rogue.h"
#include "qa/content/q2/missionpacks/items.h"
#include "qa/content/q2/missionpacks/modes/deathball.h"
#include "qa/content/q2/missionpacks/modes/tag.h"
#include "qa/content/q2/missionpacks/monsters/state.h"
#include "qa/persistence/value.h"

bool qa_read_q2_mission_pack_items_checkpoint(const qa_save_reader *reader, qa_q2mp_items_checkpoint *out);
uint8_t *qa_encode_q2_mission_pack_items_checkpoint(const qa_q2mp_items_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_mission_pack_items_checkpoint(const uint8_t *bytes, size_t length, qa_q2mp_items_checkpoint *out);

bool qa_read_q2_mission_pack_monsters_checkpoint(const qa_save_reader *reader, qa_q2mpm_monsters_checkpoint *out);
uint8_t *qa_encode_q2_mission_pack_monsters_checkpoint(const qa_q2mpm_monsters_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_mission_pack_monsters_checkpoint(const uint8_t *bytes, size_t length, qa_q2mpm_monsters_checkpoint *out);

bool qa_read_q2_rogue_entities_checkpoint(const qa_save_reader *reader, qa_q2mp_rogue_entities_checkpoint *out);
uint8_t *qa_encode_q2_rogue_entities_checkpoint(const qa_q2mp_rogue_entities_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_rogue_entities_checkpoint(const uint8_t *bytes, size_t length, qa_q2mp_rogue_entities_checkpoint *out);

bool qa_read_q2_tag_checkpoint(const qa_save_reader *reader, qa_q2mp_tag_checkpoint *out);
uint8_t *qa_encode_q2_tag_checkpoint(const qa_q2mp_tag_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_tag_checkpoint(const uint8_t *bytes, size_t length, qa_q2mp_tag_checkpoint *out);

bool qa_read_q2_death_ball_checkpoint(const qa_save_reader *reader, qa_q2mp_deathball_checkpoint *out);
uint8_t *qa_encode_q2_death_ball_checkpoint(const qa_q2mp_deathball_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_death_ball_checkpoint(const uint8_t *bytes, size_t length, qa_q2mp_deathball_checkpoint *out);

#endif
