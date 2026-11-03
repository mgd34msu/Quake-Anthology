/* Port of src/content/q1/base/grunt.ts: monsters/grunt.qc,
 * monsters/rottweiler.qc and the soldier/dog parts of fight.qc and
 * combat.qc. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_GRUNT_H
#define QA_CONTENT_Q1_BASE_GRUNT_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/base/monsters.h"

/** Shells the dying grunt drops with its solid cleared. */
void qa_q1_grunt_drop_shells(qa_q1_base_monster *monster);
void qa_q1_grunt_walk_back(qa_q1_base_monster *monster, double distance);
void qa_q1_army_fire(qa_q1_base_monster *monster);
/* touch NULL = the TS default `${callbackPrefix}:monster_jump_touch`. */
void qa_q1_dog_leap(qa_q1_base_monster *monster, const char *touch);
/** dog_bite: close in, then bite when the enemy is within reach. */
void qa_q1_dog_bite(qa_q1_base_monster *monster);
/** Frame actions of the grunt and the dog; false for every other frame. */
bool qa_q1_grunt_action(qa_q1_base_monster *monster, const char *name);
/** SoldierCheckAttack. */
bool qa_q1_army_check_attack(qa_q1_base_monster *monster);
/** DogCheckAttack. */
bool qa_q1_dog_check_attack(qa_q1_base_monster *monster);
void qa_q1_army_pain(qa_q1_base_monster *monster);
void qa_q1_dog_pain(qa_q1_base_monster *monster);
/** Dog_JumpTouch. */
void qa_q1_dog_jump_touch(qa_q1_base_monster *monster, qa_actor_id other);
/** The frame a version 5 grunt or dog sequence cursor corresponds to,
 * written to out; false (TS null) for a cursor no sequence produces. */
bool qa_q1_legacy_grunt_frame(const char *species, const char *mode, double first_frame, double frame_index,
                              char *out, size_t out_size);

#endif
