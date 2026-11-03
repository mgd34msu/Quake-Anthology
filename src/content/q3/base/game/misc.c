/* Port of src/content/q3/base/game/misc.ts. GPL-2.0-or-later. directionToByte delegated to landed shared/direction_byte.h. */
#include "qa/content/q3/base/game/misc.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/content/q3/base/shared/direction_byte.h"
#include "qa/content/q3/team_arena/client_spawn.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
void qa_q3_kill_box(qa_q3_game_combat_context *context, qa_q3_kill_box_host host, qa_q3_game_entity *entity) {
  if (!entity->client) qa_range_error("G_KillBox requires a client entity");
  qa_vec3 origin = qa_q3_base_player_state_record_origin(&entity->client->ps);
  qa_vec3 min = qa_add3(origin, qa_q3_base_entity_shared_mins(&entity->r));
  qa_vec3 max = qa_add3(origin, qa_q3_base_entity_shared_maxs(&entity->r));
  qa_bounds b = {min, max};
  qa_actor_id *contacts = malloc(1024 * sizeof *contacts); if (!contacts) qa_range_error("out of memory");
  size_t n = host.area_actors(host.ctx, b, 1024, contacts, 1024);
  qa_q3_damage_participant self_p = qa_q3_participant_entity(entity);
  for (size_t i = 0; i < n; i++) {
    if (!host.has_linked_bounds(host.ctx, contacts[i]) || !host.is_player(host.ctx, contacts[i])) continue;
    qa_q3_damage_participant hit = host.participant(host.ctx, contacts[i]);
    qa_q3_combat_damage(context, hit, &self_p, &self_p, NULL, NULL, 100000, QA_Q3_DAMAGE_NO_PROTECTION, 18);
  }
  free(contacts);
}
void qa_q3_teleport_player(qa_q3_teleport_context context, qa_q3_game_entity *player, qa_vec3 origin, qa_vec3 angles) {
  qa_q3_game_client *client = player->client;
  if (!client) qa_range_error("TeleportPlayer requires a client entity");
  if (client->sess.session_team != 3) {
    qa_q3_game_entity *out = qa_q3_entity_pool_temp_entity(context.combat->entities, qa_q3_base_player_state_record_origin(&client->ps), 43);
    out->s.client_num = player->s.client_num;
    qa_q3_game_entity *incoming = qa_q3_entity_pool_temp_entity(context.combat->entities, origin, 42);
    incoming->s.client_num = player->s.client_num;
  }
  context.world.unlink(context.world.ctx, player->s.number);
  qa_q3_base_player_state_record_set_origin(&client->ps, (qa_vec3){origin.x, origin.y, (float)origin.z + 1});
  qa_q3_base_player_state_record_set_velocity(&client->ps, qa_scale3(qa_qvm_angle_vectors(angles).forward, 400));
  client->ps.pm_time = 160; client->ps.pm_flags = qa_js_to_int32(client->ps.pm_flags) | 64; /* MoveFlags.TIME_KNOCKBACK */
  client->ps.e_flags = qa_js_to_int32(client->ps.e_flags) ^ 4;
  qa_q3_ta_set_client_view_angle(player, angles);
  if (client->sess.session_team != 3) qa_q3_kill_box(context.combat, context.kill_box, player);
  qa_q3_game_player_state_to_entity_state(&client->ps, &player->s, true, false, 0);
  qa_q3_base_entity_shared_set_current_origin(&player->r, qa_q3_base_player_state_record_origin(&client->ps));
  if (client->sess.session_team != 3) context.world.link(context.world.ctx, player);
}
void qa_q3_locate_camera(qa_q3_portal_context context, qa_q3_game_entity *entity) {
  qa_q3_game_entity *owner = qa_q3_pick_target(context.selection, entity->target);
  if (!owner) { context.selection.warn(context.selection.ctx, "Couldn't find target for misc_partal_surface\n"); qa_q3_entity_pool_free(context.selection.pool, entity); return; }
  entity->r.owner_num = owner->s.number;
  if ((qa_js_to_int32(owner->spawnflags) & 1) != 0) entity->s.frame = 25;
  else if ((qa_js_to_int32(owner->spawnflags) & 2) != 0) entity->s.frame = 75;
  entity->s.powerups = (qa_js_to_int32(owner->spawnflags) & 4) != 0 ? 0 : 1;
  entity->s.client_num = owner->s.client_num; entity->s.origin2 = owner->s.origin;
  qa_q3_game_entity *target = qa_q3_pick_target(context.selection, owner->target);
  qa_vec3 direction;
  if (target) direction = qa_normalize3(qa_sub3(target->s.origin, owner->s.origin));
  else { qa_q3_move_direction_result r = qa_q3_move_direction(owner->s.angles); direction = r.direction; owner->s.angles = r.angles; }
  entity->s.event_parm = qa_q3_base_direction_to_byte(&direction);
}
static void portal_think(void *ctx, qa_q3_game_entity *self) { qa_q3_locate_camera(*(qa_q3_portal_context *)ctx, self); }
void qa_q3_bind_portal_save_callbacks(qa_q3_portal_context context) {
  static qa_q3_portal_context retained; retained = context;
  qa_q3_entity_callback cb; qa_q3_entity_think_fn fn = portal_think; memcpy(&cb.fn, &fn, sizeof fn); cb.ctx = &retained;
  qa_q3_callback_family_intern(&context.selection.pool->callbacks.think, "q3.base.game.misc.spawnPortalSurface.think", cb);
}
void qa_q3_spawn_portal_surface(qa_q3_portal_context context, qa_q3_game_entity *entity) {
  qa_q3_bind_portal_save_callbacks(context);
  qa_q3_base_entity_shared_set_mins(&entity->r, (qa_vec3){0,0,0}); qa_q3_base_entity_shared_set_maxs(&entity->r, (qa_vec3){0,0,0});
  context.world.link(context.world.ctx, entity);
  entity->r.sv_flags = 64; entity->s.e_type = 6;
  if (!entity->target) entity->s.origin2 = entity->s.origin;
  else {
    entity->think = qa_q3_callback_family_resolve(&context.selection.pool->callbacks.think, "q3.base.game.misc.spawnPortalSurface.think");
    qa_q3_game_entity_set_nextthink(entity, qa_int32(context.time + 100));
  }
}
void qa_q3_spawn_portal_camera(qa_q3_base_server_world world, qa_q3_game_entity *entity, double roll) {
  qa_q3_base_entity_shared_set_mins(&entity->r, (qa_vec3){0,0,0}); qa_q3_base_entity_shared_set_maxs(&entity->r, (qa_vec3){0,0,0});
  world.link(world.ctx, entity);
  double packed = (float)((float)((float)roll / 360) * 256);
  entity->s.client_num = packed >= -2147483648.0 && packed < 2147483648.0 ? trunc(packed) : -2147483648.0;
}
