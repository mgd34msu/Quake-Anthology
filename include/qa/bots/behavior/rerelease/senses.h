/* Port of src/bots/behavior/rerelease/senses.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_SENSES_H
#define QA_BOTS_BEHAVIOR_RERELEASE_SENSES_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/bots/behavior/rerelease/data/botdata.h"
#include "qa/bots/behavior/rerelease/math.h"
#include "qa/bots/behavior/rerelease/world.h"
typedef struct qa_bot_awareness { int64_t id; double sight, weapon, last_contact, last_seen, last_heard; qa_bot_vec3 last_known_origin; } qa_bot_awareness;
qa_bot_awareness qa_new_awareness(int64_t id, double now, qa_bot_vec3 origin);
typedef struct qa_bot_contact { bool line_of_sight, in_sight_fov, in_weapon_fov, audible, invisible; double distance; qa_bot_vec3 origin; } qa_bot_contact;
typedef struct qa_sight_geometry { bool in_sight_fov, in_weapon_fov, within_invis_range; double distance; } qa_sight_geometry;
qa_sight_geometry qa_evaluate_sight_geometry(qa_bot_vec3 eye, double pitch, double yaw, qa_bot_vec3 target, bool invisible, qa_bot_senses_settings senses, qa_bot_weapon_sense_settings weapons);
qa_bot_awareness *qa_sense_step(qa_bot_awareness *aw, qa_bot_contact contact, qa_bot_senses_settings senses, qa_bot_weapon_sense_settings weapons, double dt, double now);
bool qa_bot_is_aware(const qa_bot_awareness *aw);
bool qa_bot_can_fire(const qa_bot_awareness *aw);
bool qa_bot_should_forget(const qa_bot_awareness *aw, qa_bot_senses_settings senses, double now);
bool qa_bot_sound_audible(qa_bot_sound sound, qa_bot_vec3 listener, qa_bot_senses_settings senses, double now);
#endif
