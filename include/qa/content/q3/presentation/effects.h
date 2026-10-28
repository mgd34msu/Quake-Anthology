/* Port of src/content/q3/presentation/effects.ts. GPL-2.0-or-later.
 * Effect constructors from id Software's code/cgame/cg_effects.c.
 * Owns only cg_effects static state; the pool owns the records. */
#ifndef QA_CONTENT_Q3_PRESENTATION_EFFECTS_H
#define QA_CONTENT_Q3_PRESENTATION_EFFECTS_H
#include <stdbool.h>
#include "qa/audio/wav.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/contracts/math.h"
/* Landed local-entity records (local_entities.h); completed there. */
typedef struct qa_q3_pres_local_entity qa_q3_pres_local_entity;
typedef struct qa_q3_pres_local_entity_pool qa_q3_pres_local_entity_pool;
typedef struct qa_q3_pres_client_game_state qa_q3_pres_client_game_state;
/* TS MissionEffectMedia. */
typedef struct qa_q3_pres_mission_effect_media {
  const qa_q3_pres_scene_shader *lightning_shader; /* nullable */
  qa_q3_pres_scene_model kamikaze_effect_model, dish_flash_model;
  const qa_q3_pres_scene_shader *rocket_explosion_shader; /* nullable */
  qa_pcm_sound *obelisk_hit_sounds[3]; /* borrowed, nullable */
  qa_q3_pres_scene_model invulnerability_impact_model;
  qa_pcm_sound *invulnerability_impact_sounds[3]; /* borrowed, nullable */
  qa_q3_pres_scene_model invulnerability_juiced_model;
  qa_pcm_sound *invulnerability_juiced_sound; /* borrowed, nullable */
} qa_q3_pres_mission_effect_media;
/* TS EffectMedia: product selects the variant fields. */
typedef struct qa_q3_pres_effect_media {
  const qa_q3_pres_scene_shader *water_bubble_shader, *smoke_puff_rage_pro_shader, *blood_explosion_shader;
  qa_q3_pres_scene_model teleport_effect_model;
  qa_q3_pres_scene_model gib_skull, gib_brain, gib_abdomen, gib_arm, gib_chest, gib_fist, gib_foot;
  qa_q3_pres_scene_model gib_forearm, gib_intestine, gib_leg, smoke2;
  qa_q3_base_product product; /* TS variant.product */
  const qa_q3_pres_scene_shader *teleport_effect_shader; /* baseq3 variant */
  qa_q3_pres_mission_effect_media mission; /* missionpack variant */
} qa_q3_pres_effect_media;
typedef enum qa_q3_pres_effect_hardware {
  QA_Q3_PRES_EFFECT_GENERIC, QA_Q3_PRES_EFFECT_RAGEPRO
} qa_q3_pres_effect_hardware;
/* TS EffectOptions. */
typedef struct qa_q3_pres_effect_options {
  bool no_projectile_trail, blood, gibs, score_plum;
  qa_q3_pres_effect_hardware hardware;
} qa_q3_pres_effect_options;
/* TS EffectImports. */
typedef struct qa_q3_pres_effect_imports {
  void *ctx;
  double (*random_integer)(void *ctx);
  void (*start_sound)(void *ctx, qa_vec3 origin, double entity, double channel, qa_pcm_sound *sound);
} qa_q3_pres_effect_imports;
/* TS SmokePuffOptions. */
typedef struct qa_q3_pres_smoke_puff_options {
  qa_vec3 origin, velocity;
  double radius;
  qa_vec4 color;
  double duration, start_time, fade_in_time, flags;
  const qa_q3_pres_scene_shader *shader; /* nullable */
} qa_q3_pres_smoke_puff_options;
/* TS ExplosionOptions. direction NULL is TS null. */
typedef struct qa_q3_pres_explosion_options {
  qa_vec3 origin;
  const qa_vec3 *direction; /* nullable */
  qa_q3_pres_scene_model model;
  const qa_q3_pres_scene_shader *shader; /* nullable */
  double duration;
  bool sprite;
} qa_q3_pres_explosion_options;
/* TS Pick views used by landed consumers. */
typedef struct qa_q3_pres_client_effects_view {
  qa_q3_pres_local_entity_pool *pool;
  void *ctx;
  qa_q3_pres_local_entity *(*smoke_puff)(void *ctx, qa_q3_pres_smoke_puff_options options);
  void (*gib_player)(void *ctx, qa_vec3 origin);
} qa_q3_pres_client_effects_view;
typedef struct qa_q3_pres_effects_smoke_view {
  void *ctx;
  qa_q3_pres_local_entity *(*smoke_puff)(void *ctx, qa_q3_pres_smoke_puff_options options);
} qa_q3_pres_effects_smoke_view;
typedef struct qa_q3_pres_event_effects_view {
  void *ctx;
  qa_q3_pres_local_entity *(*smoke_puff)(void *ctx, qa_q3_pres_smoke_puff_options options);
  qa_q3_pres_local_entity *(*spawn_effect)(void *ctx, qa_vec3 origin);
  void (*gib_player)(void *ctx, qa_vec3 origin);
  void (*score_plum)(void *ctx, double client, qa_vec3 origin, double score);
} qa_q3_pres_event_effects_view;
typedef struct qa_q3_pres_mission_effects_view {
  void *ctx;
  qa_q3_pres_local_entity *(*kamikaze_effect)(void *ctx, qa_vec3 origin);
  void (*obelisk_explode)(void *ctx, qa_vec3 origin);
  void (*obelisk_pain)(void *ctx, qa_vec3 origin);
  qa_q3_pres_local_entity *(*invulnerability_impact)(void *ctx, qa_vec3 origin, qa_vec3 angles);
  qa_q3_pres_local_entity *(*invulnerability_juiced)(void *ctx, qa_vec3 origin);
  qa_q3_pres_local_entity *(*lightning_bolt_beam)(void *ctx, qa_vec3 start, qa_vec3 end);
} qa_q3_pres_mission_effects_view;
typedef struct qa_q3_pres_client_effects {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_local_entity_pool *pool;
  qa_q3_pres_effect_media media;
  qa_q3_pres_effect_options options;
  qa_q3_pres_effect_imports imports;
  double smoke_seed;
  qa_vec3 last_score_position;
} qa_q3_pres_client_effects;
/* Product mismatches abort with the TS Error texts. */
void qa_q3_pres_client_effects_init(qa_q3_pres_client_effects *effects, qa_q3_pres_client_game_state *state,
    qa_q3_pres_local_entity_pool *pool, qa_q3_base_product pool_product,
    qa_q3_pres_effect_media media, qa_q3_pres_effect_options options, qa_q3_pres_effect_imports imports);
void qa_q3_pres_client_effects_bubble_trail(qa_q3_pres_client_effects *effects, qa_vec3 start, qa_vec3 end,
                                            double spacing);
qa_q3_pres_local_entity *qa_q3_pres_client_effects_smoke_puff(qa_q3_pres_client_effects *effects,
                                                              qa_q3_pres_smoke_puff_options options);
qa_q3_pres_local_entity *qa_q3_pres_client_effects_spawn_effect(qa_q3_pres_client_effects *effects, qa_vec3 origin);
qa_q3_pres_local_entity *qa_q3_pres_client_effects_make_explosion(qa_q3_pres_client_effects *effects,
                                                                  qa_q3_pres_explosion_options options);
void qa_q3_pres_client_effects_bleed(qa_q3_pres_client_effects *effects, qa_vec3 origin, double entity_num);
/* False is the TS null (blood disabled); out borrows pool storage. */
bool qa_q3_pres_client_effects_bleed_at(qa_q3_pres_client_effects *effects, qa_vec3 origin,
                                        bool hide_in_first_person, qa_q3_pres_ref_sprite_entity **out);
qa_q3_pres_local_entity *qa_q3_pres_client_effects_launch_gib(qa_q3_pres_client_effects *effects, qa_vec3 origin,
    qa_vec3 velocity, qa_q3_pres_scene_model model);
void qa_q3_pres_client_effects_gib_player(qa_q3_pres_client_effects *effects, qa_vec3 origin);
qa_q3_pres_local_entity *qa_q3_pres_client_effects_launch_explode(qa_q3_pres_client_effects *effects,
    qa_vec3 origin, qa_vec3 velocity, qa_q3_pres_scene_model model);
void qa_q3_pres_client_effects_big_explode(qa_q3_pres_client_effects *effects, qa_vec3 origin);
void qa_q3_pres_client_effects_score_plum(qa_q3_pres_client_effects *effects, double client, qa_vec3 origin,
                                          double score);
qa_q3_pres_local_entity *qa_q3_pres_client_effects_lightning_bolt_beam(qa_q3_pres_client_effects *effects,
    qa_vec3 start, qa_vec3 end);
qa_q3_pres_local_entity *qa_q3_pres_client_effects_kamikaze_effect(qa_q3_pres_client_effects *effects, qa_vec3 origin);
void qa_q3_pres_client_effects_obelisk_explode(qa_q3_pres_client_effects *effects, qa_vec3 origin);
void qa_q3_pres_client_effects_obelisk_pain(qa_q3_pres_client_effects *effects, qa_vec3 origin);
qa_q3_pres_local_entity *qa_q3_pres_client_effects_invulnerability_impact(qa_q3_pres_client_effects *effects,
    qa_vec3 origin, qa_vec3 angles);
qa_q3_pres_local_entity *qa_q3_pres_client_effects_invulnerability_juiced(qa_q3_pres_client_effects *effects,
                                                                          qa_vec3 origin);
qa_q3_pres_client_effects_view qa_q3_pres_client_effects_as_view(qa_q3_pres_client_effects *effects);
qa_q3_pres_effects_smoke_view qa_q3_pres_client_effects_as_smoke_view(qa_q3_pres_client_effects *effects);
qa_q3_pres_event_effects_view qa_q3_pres_client_effects_as_event_view(qa_q3_pres_client_effects *effects);
qa_q3_pres_mission_effects_view qa_q3_pres_client_effects_as_mission_view(qa_q3_pres_client_effects *effects);
#endif
