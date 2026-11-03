/* Port of src/content/q1/base/player.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_PLAYER_H
#define QA_CONTENT_Q1_BASE_PLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q1/foundation/entity_services.h"

/* TS Q1CharacterAttack mirror subset (owner foundation/types.ts). */
typedef struct qa_q1_base_character_attack {
  const char *kind; /* "axe"|"shotgun"|"rocket"|"nail"|"lightning" */
  int variant; /* axe 0..3 */
} qa_q1_base_character_attack;

/* TS Q1CharacterInput. water_type is "empty"|"water"|"slime"|"lava". */
typedef struct qa_q1_character_input {
  bool axe_pose; bool attack; bool jump; bool use;
  int water_level; const char *water_type;
  bool invisible; bool invulnerable;
} qa_q1_character_input;
/* TS Q1CharacterFrameRange / Definition / SourcePose. */
typedef struct qa_q1_character_frame_range { double first; double count; } qa_q1_character_frame_range;
typedef struct qa_q1_character_definition {
  const char *model;
  qa_q1_character_frame_range stand, run, pain, death;
} qa_q1_character_definition;
typedef struct qa_q1_character_source_pose {
  bool has_axe_pose; bool axe_pose;
  const qa_q1_character_definition *definition;
  bool has_frame; double frame; /* has_frame false = TS null */
} qa_q1_character_source_pose;
/* TS Q1CharacterOptions. source_pose fills *out and returns has-pose
 * semantics: the TS pose object itself may be undefined; model that
 * with source_pose == NULL or returning false. */
typedef struct qa_q1_character_options {
  bool has_presentation_only; bool presentation_only;
  void *ctx;
  void (*drop_inventory)(void *ctx);
  void (*request_respawn)(void *ctx);
  bool (*source_pose)(void *ctx, qa_q1_character_source_pose *out);
  bool has_fall_damage_allowed; bool (*fall_damage_allowed)(void *ctx);
} qa_q1_character_options;
/* TS Q1CharacterPresentation. life/solid/movement are TS strings. */
typedef struct qa_q1_character_presentation {
  char model[128]; double frame; qa_vec3 view_offset;
  const char *life; const char *solid; const char *movement;
  bool weapon_visible;
} qa_q1_character_presentation;

typedef struct qa_q1_character_actor qa_q1_character_actor;
void qa_q1_register_character_callbacks(qa_q1_services *game);
qa_q1_character_actor *qa_q1_character_actor_create(qa_q1_services *game, qa_owned_actor actor,
                                                    const qa_q1_character_options *options);
void qa_q1_character_actor_destroy(qa_q1_character_actor *character);
int qa_q1_character_actor_water_level(const qa_q1_character_actor *character);
uint8_t *qa_q1_character_actor_capture(qa_q1_character_actor *character, size_t *out_length);
void qa_q1_character_actor_restore(qa_q1_character_actor *character, const uint8_t *bytes, size_t length);
const char *qa_q1_character_actor_life(const qa_q1_character_actor *character);
qa_vec3 qa_q1_character_actor_view_offset(const qa_q1_character_actor *character);
qa_q1_character_presentation qa_q1_character_actor_presentation(qa_q1_character_actor *character);
qa_q1_character_presentation qa_q1_character_actor_frame(qa_q1_character_actor *character,
                                                         double seconds, qa_q1_character_input input);
void qa_q1_character_actor_attack(qa_q1_character_actor *character, qa_q1_base_character_attack attack);
void qa_q1_character_actor_pain(qa_q1_character_actor *character, const qa_actor_id *attacker,
                                double damage, bool axe_hit);
void qa_q1_character_actor_die(qa_q1_character_actor *character, const qa_actor_id *attacker);
void qa_q1_character_actor_respawn(qa_q1_character_actor *character, const double *health);
void qa_q1_character_actor_set_suicide_frame(qa_q1_character_actor *character);
void qa_q1_character_actor_post_move(qa_q1_character_actor *character);

#endif
