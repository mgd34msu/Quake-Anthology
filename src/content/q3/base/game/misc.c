/* Port of src/content/q3/base/game/misc.ts. GPL-2.0-or-later. Direction table from shared/direction-byte.ts. */
#include "qa/content/q3/base/game/misc.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
static const qa_vec3 BYTE_DIRECTIONS[162] = {
  {-0.525731, 0.000000, 0.850651},
  {-0.442863, 0.238856, 0.864188},
  {-0.295242, 0.000000, 0.955423},
  {-0.309017, 0.500000, 0.809017},
  {-0.162460, 0.262866, 0.951056},
  {0.000000, 0.000000, 1.000000},
  {0.000000, 0.850651, 0.525731},
  {-0.147621, 0.716567, 0.681718},
  {0.147621, 0.716567, 0.681718},
  {0.000000, 0.525731, 0.850651},
  {0.309017, 0.500000, 0.809017},
  {0.525731, 0.000000, 0.850651},
  {0.295242, 0.000000, 0.955423},
  {0.442863, 0.238856, 0.864188},
  {0.162460, 0.262866, 0.951056},
  {-0.681718, 0.147621, 0.716567},
  {-0.809017, 0.309017, 0.500000},
  {-0.587785, 0.425325, 0.688191},
  {-0.850651, 0.525731, 0.000000},
  {-0.864188, 0.442863, 0.238856},
  {-0.716567, 0.681718, 0.147621},
  {-0.688191, 0.587785, 0.425325},
  {-0.500000, 0.809017, 0.309017},
  {-0.238856, 0.864188, 0.442863},
  {-0.425325, 0.688191, 0.587785},
  {-0.716567, 0.681718, -0.147621},
  {-0.500000, 0.809017, -0.309017},
  {-0.525731, 0.850651, 0.000000},
  {0.000000, 0.850651, -0.525731},
  {-0.238856, 0.864188, -0.442863},
  {0.000000, 0.955423, -0.295242},
  {-0.262866, 0.951056, -0.162460},
  {0.000000, 1.000000, 0.000000},
  {0.000000, 0.955423, 0.295242},
  {-0.262866, 0.951056, 0.162460},
  {0.238856, 0.864188, 0.442863},
  {0.262866, 0.951056, 0.162460},
  {0.500000, 0.809017, 0.309017},
  {0.238856, 0.864188, -0.442863},
  {0.262866, 0.951056, -0.162460},
  {0.500000, 0.809017, -0.309017},
  {0.850651, 0.525731, 0.000000},
  {0.716567, 0.681718, 0.147621},
  {0.716567, 0.681718, -0.147621},
  {0.525731, 0.850651, 0.000000},
  {0.425325, 0.688191, 0.587785},
  {0.864188, 0.442863, 0.238856},
  {0.688191, 0.587785, 0.425325},
  {0.809017, 0.309017, 0.500000},
  {0.681718, 0.147621, 0.716567},
  {0.587785, 0.425325, 0.688191},
  {0.955423, 0.295242, 0.000000},
  {1.000000, 0.000000, 0.000000},
  {0.951056, 0.162460, 0.262866},
  {0.850651, -0.525731, 0.000000},
  {0.955423, -0.295242, 0.000000},
  {0.864188, -0.442863, 0.238856},
  {0.951056, -0.162460, 0.262866},
  {0.809017, -0.309017, 0.500000},
  {0.681718, -0.147621, 0.716567},
  {0.850651, 0.000000, 0.525731},
  {0.864188, 0.442863, -0.238856},
  {0.809017, 0.309017, -0.500000},
  {0.951056, 0.162460, -0.262866},
  {0.525731, 0.000000, -0.850651},
  {0.681718, 0.147621, -0.716567},
  {0.681718, -0.147621, -0.716567},
  {0.850651, 0.000000, -0.525731},
  {0.809017, -0.309017, -0.500000},
  {0.864188, -0.442863, -0.238856},
  {0.951056, -0.162460, -0.262866},
  {0.147621, 0.716567, -0.681718},
  {0.309017, 0.500000, -0.809017},
  {0.425325, 0.688191, -0.587785},
  {0.442863, 0.238856, -0.864188},
  {0.587785, 0.425325, -0.688191},
  {0.688191, 0.587785, -0.425325},
  {-0.147621, 0.716567, -0.681718},
  {-0.309017, 0.500000, -0.809017},
  {0.000000, 0.525731, -0.850651},
  {-0.525731, 0.000000, -0.850651},
  {-0.442863, 0.238856, -0.864188},
  {-0.295242, 0.000000, -0.955423},
  {-0.162460, 0.262866, -0.951056},
  {0.000000, 0.000000, -1.000000},
  {0.295242, 0.000000, -0.955423},
  {0.162460, 0.262866, -0.951056},
  {-0.442863, -0.238856, -0.864188},
  {-0.309017, -0.500000, -0.809017},
  {-0.162460, -0.262866, -0.951056},
  {0.000000, -0.850651, -0.525731},
  {-0.147621, -0.716567, -0.681718},
  {0.147621, -0.716567, -0.681718},
  {0.000000, -0.525731, -0.850651},
  {0.309017, -0.500000, -0.809017},
  {0.442863, -0.238856, -0.864188},
  {0.162460, -0.262866, -0.951056},
  {0.238856, -0.864188, -0.442863},
  {0.500000, -0.809017, -0.309017},
  {0.425325, -0.688191, -0.587785},
  {0.716567, -0.681718, -0.147621},
  {0.688191, -0.587785, -0.425325},
  {0.587785, -0.425325, -0.688191},
  {0.000000, -0.955423, -0.295242},
  {0.000000, -1.000000, 0.000000},
  {0.262866, -0.951056, -0.162460},
  {0.000000, -0.850651, 0.525731},
  {0.000000, -0.955423, 0.295242},
  {0.238856, -0.864188, 0.442863},
  {0.262866, -0.951056, 0.162460},
  {0.500000, -0.809017, 0.309017},
  {0.716567, -0.681718, 0.147621},
  {0.525731, -0.850651, 0.000000},
  {-0.238856, -0.864188, -0.442863},
  {-0.500000, -0.809017, -0.309017},
  {-0.262866, -0.951056, -0.162460},
  {-0.850651, -0.525731, 0.000000},
  {-0.716567, -0.681718, -0.147621},
  {-0.716567, -0.681718, 0.147621},
  {-0.525731, -0.850651, 0.000000},
  {-0.500000, -0.809017, 0.309017},
  {-0.238856, -0.864188, 0.442863},
  {-0.262866, -0.951056, 0.162460},
  {-0.864188, -0.442863, 0.238856},
  {-0.809017, -0.309017, 0.500000},
  {-0.688191, -0.587785, 0.425325},
  {-0.681718, -0.147621, 0.716567},
  {-0.442863, -0.238856, 0.864188},
  {-0.587785, -0.425325, 0.688191},
  {-0.309017, -0.500000, 0.809017},
  {-0.147621, -0.716567, 0.681718},
  {-0.425325, -0.688191, 0.587785},
  {-0.162460, -0.262866, 0.951056},
  {0.442863, -0.238856, 0.864188},
  {0.162460, -0.262866, 0.951056},
  {0.309017, -0.500000, 0.809017},
  {0.147621, -0.716567, 0.681718},
  {0.000000, -0.525731, 0.850651},
  {0.425325, -0.688191, 0.587785},
  {0.587785, -0.425325, 0.688191},
  {0.688191, -0.587785, 0.425325},
  {-0.955423, 0.295242, 0.000000},
  {-0.951056, 0.162460, 0.262866},
  {-1.000000, 0.000000, 0.000000},
  {-0.850651, 0.000000, 0.525731},
  {-0.955423, -0.295242, 0.000000},
  {-0.951056, -0.162460, 0.262866},
  {-0.864188, 0.442863, -0.238856},
  {-0.951056, 0.162460, -0.262866},
  {-0.809017, 0.309017, -0.500000},
  {-0.864188, -0.442863, -0.238856},
  {-0.951056, -0.162460, -0.262866},
  {-0.809017, -0.309017, -0.500000},
  {-0.681718, 0.147621, -0.716567},
  {-0.681718, -0.147621, -0.716567},
  {-0.850651, 0.000000, -0.525731},
  {-0.688191, 0.587785, -0.425325},
  {-0.587785, 0.425325, -0.688191},
  {-0.425325, 0.688191, -0.587785},
  {-0.425325, -0.688191, -0.587785},
  {-0.587785, -0.425325, -0.688191},
  {-0.688191, -0.587785, -0.425325}
};
double qa_q3_direction_to_byte_mirror(qa_vec3 value) {
  double best_dot = 0; int best = 0;
  for (int i = 0; i < 162; i++) { double d = qa_dot3(value, BYTE_DIRECTIONS[i]); if (d > best_dot) { best_dot = d; best = i; } }
  return best;
}
void qa_q3_kill_box(qa_q3_combat_context *context, qa_q3_kill_box_host host, qa_q3_game_entity *entity) {
  if (!entity->client) qa_range_error("G_KillBox requires a client entity");
  qa_vec3 origin = entity->client->ps.origin;
  qa_bounds b = {qa_add3(origin, entity->r.mins), qa_add3(origin, entity->r.maxs)};
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
static void set_client_view_angle_mirror(qa_q3_game_entity *entity, qa_vec3 angles) {
  qa_q3_game_client *client = entity->client; qa_vec3 cmd = client->pers.cmd.angles;
  double vals[3] = {angles.x, angles.y, angles.z}, cmds[3] = {cmd.x, cmd.y, cmd.z};
  for (int i = 0; i < 3; i++) {
    double scaled = (float)((float)(vals[i] * 65536) / 360);
    int sh = qa_qvm_float_to_int(scaled) & 65535;
    qa_q3_game_slots_set_mirror(&client->ps.delta_angles, (size_t)i, qa_int32(sh - cmds[i]));
  }
  entity->s.angles = angles; client->ps.viewangles = angles;
}
void qa_q3_teleport_player(qa_q3_teleport_context context, qa_q3_game_entity *player, qa_vec3 origin, qa_vec3 angles) {
  qa_q3_game_client *client = player->client;
  if (!client) qa_range_error("TeleportPlayer requires a client entity");
  if (client->sess.session_team != 3) {
    qa_q3_game_entity *out = qa_q3_entity_pool_temp_entity(context.combat->entities, client->ps.origin, 43);
    out->s.client_num = player->s.client_num;
    qa_q3_game_entity *incoming = qa_q3_entity_pool_temp_entity(context.combat->entities, origin, 42);
    incoming->s.client_num = player->s.client_num;
  }
  context.world.unlink_number(context.world.ctx, player->s.number);
  client->ps.origin = (qa_vec3){origin.x, origin.y, (float)origin.z + 1};
  client->ps.velocity = qa_scale3(qa_qvm_angle_vectors(angles).forward, 400);
  client->ps.pm_time = 160; client->ps.pm_flags = (int)client->ps.pm_flags | 1;
  client->ps.e_flags = (int)client->ps.e_flags ^ 4;
  set_client_view_angle_mirror(player, angles);
  if (client->sess.session_team != 3) qa_q3_kill_box(context.combat, context.kill_box, player);
  /* playerStateToEntityState mirror: copy origin/angles into s. */
  player->s.origin = client->ps.origin; player->r.current_origin = client->ps.origin;
  if (client->sess.session_team != 3) context.world.link(context.world.ctx, player);
}
void qa_q3_locate_camera(qa_q3_portal_context context, qa_q3_game_entity *entity) {
  qa_q3_game_entity *owner = qa_q3_pick_target(context.selection, entity->target);
  if (!owner) { context.selection.warn(context.selection.ctx, "Couldn't find target for misc_partal_surface\n"); qa_q3_entity_pool_free(context.selection.pool, entity); return; }
  entity->r.owner_num = owner->s.number;
  if (((int)owner->spawnflags & 1) != 0) entity->s.frame = 25;
  else if (((int)owner->spawnflags & 2) != 0) entity->s.frame = 75;
  entity->s.powerups = ((int)owner->spawnflags & 4) != 0 ? 0 : 1;
  entity->s.client_num = owner->s.client_num; entity->s.origin2 = owner->s.origin;
  qa_q3_game_entity *target = qa_q3_pick_target(context.selection, owner->target);
  qa_vec3 direction;
  if (target) direction = qa_normalize3(qa_sub3(target->s.origin, owner->s.origin));
  else { qa_q3_move_direction_result r = qa_q3_move_direction(owner->s.angles); direction = r.direction; owner->s.angles = r.angles; }
  entity->s.event_parm = qa_q3_direction_to_byte_mirror(direction);
}
static void portal_think(void *ctx, qa_q3_game_entity *self) { qa_q3_locate_camera(*(qa_q3_portal_context *)ctx, self); }
void qa_q3_bind_portal_save_callbacks(qa_q3_portal_context context) {
  static qa_q3_portal_context retained; retained = context;
  qa_q3_entity_callback cb; qa_q3_entity_think_fn fn = portal_think; memcpy(&cb.fn, &fn, sizeof fn); cb.ctx = &retained;
  qa_q3_callback_family_intern(&context.selection.pool->callbacks.think, "q3.base.game.misc.spawnPortalSurface.think", cb);
}
void qa_q3_spawn_portal_surface(qa_q3_portal_context context, qa_q3_game_entity *entity) {
  qa_q3_bind_portal_save_callbacks(context);
  entity->r.mins = (qa_vec3){0,0,0}; entity->r.maxs = (qa_vec3){0,0,0};
  context.world.link(context.world.ctx, entity);
  entity->r.sv_flags = 64; entity->s.e_type = 6;
  if (!entity->target) entity->s.origin2 = entity->s.origin;
  else {
    entity->think = qa_q3_callback_family_resolve(&context.selection.pool->callbacks.think, "q3.base.game.misc.spawnPortalSurface.think");
    qa_q3_game_entity_set_nextthink(entity, qa_int32(context.time + 100));
  }
}
void qa_q3_spawn_portal_camera(qa_q3_server_world_mirror world, qa_q3_game_entity *entity, double roll) {
  entity->r.mins = (qa_vec3){0,0,0}; entity->r.maxs = (qa_vec3){0,0,0};
  world.link(world.ctx, entity);
  double packed = (float)((float)((float)roll / 360) * 256);
  entity->s.client_num = packed >= -2147483648.0 && packed < 2147483648.0 ? trunc(packed) : -2147483648.0;
}
