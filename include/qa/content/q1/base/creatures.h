/* Port of src/content/q1/base/creatures.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_CREATURES_H
#define QA_CONTENT_Q1_BASE_CREATURES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/base/monsters.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/persistence/value.h"

typedef struct qa_q1_wizard_shot { qa_actor_id enemy; qa_vec3 right; } qa_q1_wizard_shot;

typedef struct qa_q1_creatures qa_q1_creatures;

/* TS q1Creatures(game): aborts when services were not registered. */
qa_q1_creatures *qa_q1_creatures_for(qa_q1_services *game);
/* services provides countMonsterKill/finale/finishFinale. */
qa_q1_creatures *qa_q1_creatures_create(qa_q1_services *game, qa_q1_monster_services services);
void qa_q1_creatures_destroy(qa_q1_creatures *creatures);
qa_q1_services *qa_q1_creatures_game(qa_q1_creatures *creatures);
/* This object as TS MonsterServices for BaseMonster construction. */
qa_q1_monster_services qa_q1_creatures_as_services(qa_q1_creatures *creatures);
void qa_q1_creatures_register_species(qa_q1_creatures *creatures,
                                      const qa_q1_base_monster_species *species, size_t count);
bool qa_q1_creatures_count_monster_kill(qa_q1_creatures *creatures, qa_q1_base_monster *monster);
void qa_q1_creatures_finale(qa_q1_creatures *creatures, qa_q1_base_monster *monster);
void qa_q1_creatures_finish_finale(qa_q1_creatures *creatures, qa_q1_base_monster *monster);
void qa_q1_creatures_clone(qa_q1_creatures *creatures, qa_q1_actor *source, qa_q1_actor *target);
/* captureFields(): sets hellKnightType/monsters/backpacks/targets/shots on root. */
bool qa_q1_creatures_capture_fields(qa_q1_creatures *creatures, qa_save_value *root);
void qa_q1_creatures_restore_fields(qa_q1_creatures *creatures, const qa_save_reader *root);
const char *qa_q1_creatures_next_hell_knight_melee(qa_q1_creatures *creatures);

/* TS Map surfaces (monsters/backpacks/projectileTargets/wizardShots). */
qa_q1_base_monster *qa_q1_creatures_monsters_get(qa_q1_creatures *creatures, qa_owned_actor actor);
void qa_q1_creatures_monsters_set(qa_q1_creatures *creatures, qa_owned_actor actor, qa_q1_base_monster *monster);
void qa_q1_creatures_monsters_delete(qa_q1_creatures *creatures, qa_owned_actor actor);
size_t qa_q1_creatures_monsters_list(qa_q1_creatures *creatures, qa_q1_base_monster **out, size_t capacity);
/* Monsters from insertion position `first` on, at most `capacity`; returns the count written. */
size_t qa_q1_creatures_monsters_list_from(qa_q1_creatures *creatures, size_t first,
                                          qa_q1_base_monster **out, size_t capacity);
const qa_q1_backpack_contents *qa_q1_creatures_backpacks_get(qa_q1_creatures *creatures, qa_owned_actor actor);
void qa_q1_creatures_backpacks_set(qa_q1_creatures *creatures, qa_owned_actor actor,
                                   const qa_q1_backpack_contents *contents);
void qa_q1_creatures_backpacks_delete(qa_q1_creatures *creatures, qa_owned_actor actor);
bool qa_q1_creatures_targets_get(qa_q1_creatures *creatures, qa_owned_actor actor, qa_actor_id *out);
void qa_q1_creatures_targets_set(qa_q1_creatures *creatures, qa_owned_actor actor, qa_actor_id enemy);
const qa_q1_wizard_shot *qa_q1_creatures_shots_get(qa_q1_creatures *creatures, qa_owned_actor actor);
void qa_q1_creatures_shots_set(qa_q1_creatures *creatures, qa_owned_actor actor, qa_q1_wizard_shot shot);

#endif
