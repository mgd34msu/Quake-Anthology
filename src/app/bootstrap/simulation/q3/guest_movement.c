/* Port of src/app/bootstrap/simulation/q3/guest-movement.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/guest_movement.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/network/q3/adapters.h"

struct qa_app_q3_guest_movement_projection {
  qa_app_q3_guest_movement_host host;
  qa_app_q3_player player;
  qa_executable_recipe recipe;
  qa_app_sim_locomotion_player template;
  const char *product;
  qa_provider_id provider;
  qa_inventory_entry ammo[26];
  qa_actor_id jump_pad_storage;
};

static double view_at(qa_double_view view, double index) {
  size_t i = (size_t)index;
  return index >= 0 && i < view.count ? view.values[i] : 0;
}
qa_app_q3_guest_movement_projection *qa_app_guest_movement_projection_create(qa_app_q3_guest_movement_host host,
    qa_app_q3_player player, qa_executable_recipe recipe) {
  qa_app_sim_locomotion_player template = qa_app_sim_locomotion_template(recipe);
  if (!template.profile.kind || strcmp(template.profile.kind, "q3") != 0)
    qa_range_error("QVM movement projection requires its selected movement adapter");
  qa_app_q3_guest_movement_projection *projection = calloc(1, sizeof *projection);
  if (!projection) return NULL;
  projection->host = host; projection->player = player; projection->recipe = recipe; projection->template = template;
  projection->product = recipe.map.entities.content && strstr(recipe.map.entities.content, ":missionpack:") ? "missionpack" : "baseq3";
  projection->provider = recipe.weapons.count > 0 && recipe.weapons.values ? recipe.weapons.values[0].provider : recipe.map.entities.provider;
  return projection;
}
void qa_app_q3_guest_movement_projection_destroy(qa_app_q3_guest_movement_projection *projection) { free(projection); }

qa_app_sim_movement_prediction_player qa_app_q3_guest_movement_projection_refresh(qa_app_q3_guest_movement_projection *projection) {
  qa_q3_player_state ps = projection->host.player_state(projection->host.ctx, projection->player.source_entity);
  bool missionpack = strcmp(projection->product, "missionpack") == 0;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(missionpack ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3);
  qa_app_sim_movement_prediction_player player; memset(&player, 0, sizeof player);
  player.profile = projection->template.profile;
  player.standing_bounds = projection->template.standing_bounds;
  player.character = projection->template.character;
  player.client = projection->player.client;
  player.actor = projection->host.actor_at(projection->host.ctx, projection->player.source_entity);
  const qa_numeric_operations *numeric = qa_create_numeric_operations(&player.profile.numeric);
  if (numeric) player.numeric = *numeric;
  player.gravity_multiplier = 1; player.movement_speed_multiplier = 1;
  player.flight = view_at(ps.powerups, QA_Q3_PW_FLIGHT) != 0;
  player.world_gravity = ps.gravity;
  player.has_source_environment = true;
  player.source_environment.health = view_at(ps.stats, 0);
  player.source_environment.flight = player.flight;
  player.source_environment.haste = view_at(ps.powerups, QA_Q3_PW_HASTE) != 0;
  player.source_environment.invulnerable = missionpack && view_at(ps.powerups, QA_Q3_PW_INVULNERABILITY) != 0;
  player.source_environment.gravity_multiplier = 1;
  player.has_source_movement = true;
  player.source_movement.trace_mask = 0;
  /* ClientMovementOptions traceMask/fixed/noFootsteps carried in the
   * landed options struct (trace_mask via has flag in players.h type). */
  player.source_movement.has_trace_mask = true;
  player.source_movement.trace_mask = view_at(ps.stats, 0) <= 0 ? 0x10001 : 0x02010001;
  double fixed = projection->host.cvar_value(projection->host.ctx, "pmove_fixed");
  if (fixed != 0) {
    double msec = projection->host.cvar_value(projection->host.ctx, "pmove_msec");
    player.source_movement.has_fixed_msec = true;
    player.source_movement.fixed_msec = msec < 8 ? 8 : msec > 33 ? 33 : msec;
  }
  double dmflags = projection->host.cvar_value(projection->host.ctx, "dmflags");
  player.source_movement.no_footsteps = dmflags != 0 && (((long long)dmflags) & 32) != 0;
  qa_vec3 mins, maxs;
  projection->host.entity_bounds(projection->host.ctx, projection->player.source_entity, &mins, &maxs);
  player.bounds = (qa_bounds){mins, maxs};
  player.view_height = ps.view_height;
  player.state.kind = QA_MOVEMENT_STATE_Q3;
  qa_q3_movement_state *q3 = &player.state.as.q3;
  q3->command_time_milliseconds = ps.command_time_milliseconds;
  q3->movement_type = ps.movement_type; q3->bob_cycle = ps.bob_cycle;
  q3->movement_flags = ps.movement_flags; q3->movement_time_milliseconds = ps.movement_time_milliseconds;
  q3->origin = ps.origin; q3->velocity = ps.velocity; q3->gravity = ps.gravity; q3->speed = ps.speed;
  q3->delta_angle_words = ps.delta_angle_words;
  if (ps.ground_entity_number == 1022) { q3->ground.kind = QA_TRACE_HIT_WORLD; q3->ground.model = 0; }
  else if (ps.ground_entity_number == 1023) q3->ground.kind = QA_TRACE_HIT_NONE;
  else {
    q3->ground.kind = QA_TRACE_HIT_ACTOR;
    q3->ground.actor = projection->host.actor_at(projection->host.ctx, ps.ground_entity_number).id;
  }
  q3->movement_direction = ps.movement_direction; q3->grapple_point = ps.grapple_point; q3->flags = ps.flags;
  q3->view_angles = ps.view_angles; q3->view_height = ps.view_height;
  q3->predictable_event_sequence = ps.event_sequence;
  if (ps.jump_pad_entity != 0 && projection->host.reference_at
      && projection->host.reference_at(projection->host.ctx, ps.jump_pad_entity, &projection->jump_pad_storage))
    q3->jump_pad = &projection->jump_pad_storage;
  q3->movement_frame = ps.movement_frame_count; q3->jump_pad_frame = ps.jump_pad_frame;
  qa_arsenal_state original;
  if (projection->host.player_arsenal && projection->host.player_arsenal(projection->host.ctx, projection->player.actor, &original)) {
    player.arsenal = original;
  } else {
    player.arsenal.provider = projection->provider;
    const qa_q3_weapon_item *active = qa_q3_get_weapon_item(ps.weapon);
    player.arsenal.active_weapon = active ? active->item : NULL;
    player.arsenal.state.kind = "q3"; player.arsenal.state.source_weapon = ps.weapon;
    player.arsenal.state.state = ps.weapon_state; player.arsenal.state.time_milliseconds = ps.weapon_time_milliseconds;
    size_t count = 0;
    for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++) {
      const qa_q3_weapon_item *item = &qa_q3_weapon_items[i];
      if (!missionpack && item->weapon >= 11) continue;
      memset(&projection->ammo[count], 0, sizeof projection->ammo[count]);
      projection->ammo[count].item = item->item;
      projection->ammo[count].count = (((long long)view_at(ps.stats, schema->weapons)) & (1LL << (long long)item->weapon)) == 0 ? 0 : 1;
      projection->ammo[count].capacity = 1; count++;
      if (item->ammo) {
        memset(&projection->ammo[count], 0, sizeof projection->ammo[count]);
        projection->ammo[count].item = item->ammo;
        projection->ammo[count].count = view_at(ps.ammo, item->weapon);
        projection->ammo[count].capacity = 200; count++;
      }
    }
    player.arsenal.ammo = projection->ammo; player.arsenal.ammo_count = count;
  }
  player.animation.provider = projection->recipe.character.definition.provider;
  player.animation.state.kind = "q3";
  player.animation.state.legs = ps.legs_animation; player.animation.state.torso = ps.torso_animation;
  player.animation.state.legs_timer_milliseconds = ps.legs_timer_milliseconds;
  player.animation.state.torso_timer_milliseconds = ps.torso_timer_milliseconds;
  qa_q3_player_state_record record;
  qa_q3_from_player_state(&ps, missionpack ? QA_Q3_PRODUCT_MISSIONPACK : QA_Q3_PRODUCT_BASEQ3, &record);
  double holdable = (double)record.stats[(size_t)schema->holdable_item < 16 ? (size_t)schema->holdable_item : 0];
  qa_q3_arsenal_runtime_state runtime = qa_q3_spawn_arsenal_runtime(projection->product, (double)record.stats[0], 0);
  runtime.max_health = (double)record.stats[(size_t)schema->max_health < 16 ? (size_t)schema->max_health : 0];
  runtime.spectator = ps.movement_type == 1;
  const qa_q3_base_item_definition *persistent = NULL, *held = NULL;
  if (schema->has_persistent_powerup && schema->product == QA_Q3_BASE_PRODUCT_MISSIONPACK)
    persistent = qa_q3_base_item_at(missionpack ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3,
        (double)record.stats[(size_t)schema->persistent_powerup < 16 ? (size_t)schema->persistent_powerup : 0]);
  held = qa_q3_base_item_at(missionpack ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3, holdable);
  runtime.persistent_powerup_tag = persistent ? persistent->tag : 0;
  runtime.holdable_item = holdable; runtime.holdable_tag = held ? held->tag : 0;
  runtime.respawned = (((long long)ps.movement_flags) & QA_Q3_FLAG_RESPAWNED) != 0;
  runtime.use_item_held = (((long long)ps.movement_flags) & QA_Q3_FLAG_USE_ITEM_HELD) != 0;
  runtime.event_sequence = ps.event_sequence;
  player.has_q3_arsenal = true; player.q3_arsenal = runtime;
  return player;
}
