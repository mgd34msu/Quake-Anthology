/* Port of src/content/q2/rerelease/entities.ts. GPL-2.0-or-later.
 * story/sky.name/poi.image are owned strings. */
#ifndef QA_CONTENT_Q2_RERELEASE_ENTITIES_H
#define QA_CONTENT_Q2_RERELEASE_ENTITIES_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/rerelease/campaign.h"
#include "qa/content/q2/rerelease/players.h"
#include "qa/content/q2/rerelease/types.h"

typedef struct qa_q2rr_health_bar { bool has; qa_actor_id controller, target; bool has_dead_until; double dead_until; } qa_q2rr_health_bar;
typedef struct qa_q2rr_actor_pair { qa_actor_id key, value; } qa_q2rr_actor_pair;
typedef struct qa_q2rr_poi { bool has; qa_actor_id actor; qa_vec3 origin; char *image; bool has_dynamic; qa_actor_id dynamic; } qa_q2rr_poi;
typedef struct qa_q2_rerelease_entities {
  qa_q2_rerelease_players *players;
  qa_q2_rerelease_hooks hooks;
  qa_q2_rerelease_campaign_state campaign; /* borrowed if external, else owned */
  bool owns_campaign;
  qa_q2_fog_state world_fog;
  char *story;
  struct { char *name; double rotation; bool auto_rotate; qa_vec3 axis; } sky;
  qa_q2rr_poi poi;
  double poi_stage, last_auto_save;
  qa_q2rr_health_bar health_bars[2];
  qa_q2rr_actor_pair *health_targets; size_t ht_count, ht_cap;
  qa_q2_use poi_use, flare_use, music_use, sky_use, cross_unit_use, autosave_use,
            achievement_use, story_use, change_level_use, healthbar_use, world_text_use, coop_relay_use;
  qa_q2_think poi_setup, coop_relay_think, cross_unit_think, healthbar_check, world_text_think;
  qa_q2_touch flashlight_touch, fog_touch;
} qa_q2_rerelease_entities;
/* campaign NULL = fresh owned state. */
bool qa_q2_rerelease_entities_init(qa_q2_rerelease_entities *e, qa_q2_rerelease_players *p,
                                   qa_q2_rerelease_hooks h, qa_q2_rerelease_campaign_state *campaign);
void qa_q2_rerelease_entities_destroy(qa_q2_rerelease_entities *e);
void qa_q2_rerelease_entities_end_of_unit(qa_q2_rerelease_entities *e, qa_q2_game_services *g);
void qa_q2_rerelease_entities_transfer_healthbar_target(qa_q2_rerelease_entities *e, qa_actor_id old_a, qa_actor_id new_a, qa_q2_game_services *g);
bool qa_q2_rerelease_entities_spawn(qa_q2_rerelease_entities *e, qa_q2_entity *ent, qa_q2_game_services *g);
void qa_q2_rerelease_entities_toggle_flashlight(qa_q2_rerelease_entities *e, qa_actor_id a, qa_q2_game_services *g, bool enabled);
void qa_q2_rerelease_entities_force_fog(qa_q2_rerelease_entities *e, qa_actor_id a, bool instant);
void qa_q2_rerelease_entities_end_player_frame(qa_q2_rerelease_entities *e, qa_q2_entity *ent, qa_q2_game_services *g);
qa_q2_callback_definitions qa_q2_rerelease_entities_callbacks(qa_q2_rerelease_entities *e);
qa_q2_spawn_module qa_q2_rerelease_entities_as_spawn(qa_q2_rerelease_entities *e);
bool qa_q2rr_health_target_set(qa_q2_rerelease_entities *e, qa_actor_id k, qa_actor_id v);
bool qa_q2rr_health_target_get(qa_q2_rerelease_entities *e, qa_actor_id k, qa_actor_id *out);
void qa_q2rr_health_target_delete(qa_q2_rerelease_entities *e, qa_actor_id k);
void qa_q2rr_health_target_clear(qa_q2_rerelease_entities *e);
#endif
