/*
 * Port of src/world/gameplay/weapon-behaviors.ts. Checkpoint
 * attachment arrays are malloc'd (free with
 * qa_free_weapon_behavior_attachment_checkpoint).
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_GAMEPLAY_WEAPON_BEHAVIORS_H
#define QA_WORLD_GAMEPLAY_WEAPON_BEHAVIORS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/contracts/world.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/registry.h"

typedef struct qa_weapon_behavior_attachments qa_weapon_behavior_attachments;
/* NULL only on OOM. */
qa_weapon_behavior_attachments *qa_weapon_behavior_attachments_create(
    qa_session_actor_registry *registry);
void qa_weapon_behavior_attachments_destroy(qa_weapon_behavior_attachments *attachments);
void qa_weapon_behavior_attachments_register(qa_weapon_behavior_attachments *attachments,
                                             qa_weapon_behavior_source source);
bool qa_weapon_behavior_attachments_attach(qa_weapon_behavior_attachments *attachments,
                                           const char *selection,
                                           qa_weapon_behavior_launch launch);
qa_weapon_trajectory_update_result qa_weapon_behavior_attachments_launch(
    qa_weapon_behavior_attachments *attachments, const char *selection,
    qa_weapon_behavior_launch input);
bool qa_weapon_behavior_attachments_controls_trajectory(qa_weapon_behavior_attachments *attachments,
                                                        qa_actor_id projectile);
qa_weapon_trajectory_update_result qa_weapon_behavior_attachments_step(
    qa_weapon_behavior_attachments *attachments, qa_owned_actor projectile, qa_body_state body,
    double time_seconds);
void qa_weapon_behavior_attachments_detach(qa_weapon_behavior_attachments *attachments,
                                           qa_actor_id actor);
qa_weapon_behavior_attachment_checkpoint qa_weapon_behavior_attachments_checkpoint(
    qa_weapon_behavior_attachments *attachments);
void qa_free_weapon_behavior_attachment_checkpoint(
    qa_weapon_behavior_attachment_checkpoint checkpoint);
void qa_weapon_behavior_attachments_restore(qa_weapon_behavior_attachments *attachments,
                                            qa_weapon_behavior_attachment_checkpoint checkpoint);
void qa_weapon_behavior_attachments_close(qa_weapon_behavior_attachments *attachments);

qa_weapon_behavior_definition qa_read_weapon_behavior_definition(
    const qa_save_reader *reader, qa_weapon_behavior_definition expected);
qa_weapon_behavior_attachment_checkpoint qa_read_weapon_behavior_attachment_checkpoint(
    const qa_save_reader *reader, const qa_weapon_behavior_definition *definitions,
    size_t definition_count);

#endif
