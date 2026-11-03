/* Port of src/content/q1/missionpacks/world/rogue-pendulum.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_pendulum.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"

/* frame, min, max, low, high, delay, impact, sound */
static const double SWINGS[26][8] = {
  {0, -176, -120, 48, 128, 0.17, 1, 0}, {1, -172, -112, 12, 88, 0.15, 0, 0}, {2, -160, -96, -22, 50, 0.13, 0, 0},
  {3, -138, -70, -51, 17, 0.11, 0, 1}, {4, -110, -38, -72, -8, 0.09, 0, 0}, {5, -76, 0, -83, -23, 0.07, 0, 0},
  {6, -40, 40, -88, -32, 0.05, 0, 0}, {7, 0, 76, -83, -23, 0.07, 0, 0}, {8, 38, 100, -72, -8, 0.09, 0, 0},
  {9, 70, 138, -51, 17, 0.11, 0, 0}, {10, 96, 160, -22, 50, 0.13, 0, 0}, {11, 112, 172, 12, 88, 0.15, 0, 0},
  {12, 120, 176, 48, 128, 0.17, 0, 0}, {12, 120, 176, 48, 128, 0.17, 0, 0}, {11, 112, 172, 12, 88, 0.15, -1, 0},
  {10, 96, 160, -22, 50, 0.13, 0, 0}, {9, 70, 138, -51, 17, 0.11, 0, 1}, {8, 38, 100, -72, -8, 0.09, 0, 0},
  {7, 0, 76, -83, -23, 0.07, 0, 0}, {6, -40, 40, -88, -32, 0.05, 0, 0}, {5, -76, 0, -83, -23, 0.07, 0, 0},
  {4, -110, -28, -72, -8, 0.09, 0, 0}, {3, -172, -70, -51, 17, 0.11, 0, 0}, {2, -160, -96, -22, 50, 0.13, 0, 0},
  {1, -172, -112, 12, 88, 0.15, 0, 0}, {0, -176, -120, 48, 128, 0.17, 0, 0},
};
#define SWING_COUNT (sizeof SWINGS / sizeof SWINGS[0])
static const char *const SWING_NAMES[SWING_COUNT] = {
  "rogue:pend_swing1", "rogue:pend_swing2", "rogue:pend_swing3", "rogue:pend_swing4", "rogue:pend_swing5",
  "rogue:pend_swing6", "rogue:pend_swing7", "rogue:pend_swing8", "rogue:pend_swing9", "rogue:pend_swing10",
  "rogue:pend_swing11", "rogue:pend_swing12", "rogue:pend_swing13", "rogue:pend_swing14", "rogue:pend_swing15",
  "rogue:pend_swing16", "rogue:pend_swing17", "rogue:pend_swing18", "rogue:pend_swing19", "rogue:pend_swing20",
  "rogue:pend_swing21", "rogue:pend_swing22", "rogue:pend_swing23", "rogue:pend_swing24", "rogue:pend_swing25",
  "rogue:pend_swing26",
};

static void swing(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  const size_t index = (size_t)((const double *)ctx - &SWINGS[0][0]) / 8;
  const double *row = SWINGS[index];
  entity->frame = row[0];
  if (index != 13) {
    const qa_bounds bounds = (qa_js_to_int32(entity->spawnflags) & 2) != 0
        ? (qa_bounds){{-8, row[1], row[3]}, {8, row[2], row[4]}}
        : (qa_bounds){{row[1], -8, row[3]}, {row[2], 8, row[4]}};
    qa_q1_set_bounds(game, entity, bounds);
  }
  if (row[6] != 0 && game->world != NULL) qa_q1mp_world_number(game->world, "rogue:impactVelocity", row[6]);
  if (row[7] != 0) {
    qa_q1_event event = {0};
    event.kind = "sound"; event.has_actor = true; event.actor = entity->actor.id; event.path = "pendulum/swing.wav";
    event.sound_channel = "auto"; event.volume = 0.5; event.attenuation = 1;
    qa_q1_emit(game, event);
  }
  qa_q1mp_world_later(game, entity, row[5], SWING_NAMES[(index + 1) % SWING_COUNT]);
}
static void pend_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                       const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (qa_q1_health(game, other) < 1) return;
  const qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, other);
  if (!combat.has_value || !combat.value.can_take_damage) return;
  if (entity->attack_finished < game->time) {
    qa_q1_sound(game, entity->actor.id, "pendulum/hit.wav", "voice", 1, 1);
    entity->attack_finished = game->time + 1;
  }
  (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, qa_q1_actor_number(entity, "currentammo"), NULL, NULL, NULL, NULL);
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, other);
  qa_owned_actor owner;
  const bool resolved = qa_session_actor_registry_resolve_owned(game->host.actors, other, &owner);
  if (!body.has_value || !resolved) return;
  const double impact = game->world != NULL ? qa_q1_actor_number(game->world, "rogue:impactVelocity") : 0;
  qa_body_state next = body.value;
  if ((qa_js_to_int32(entity->spawnflags) & 2) != 0) { next.velocity.y = impact * -250; next.velocity.z = 200; }
  else { next.velocity.x = impact * 250; next.velocity.z = 200; }
  qa_shared_body_table_write(game->host.bodies, owner, next);
  qa_q1_effect(game, "meat-spray", body.value.origin, &other, 1);
}
static void pend_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  qa_q1mp_world_later(game, entity, entity->delay, "rogue:pend_swing1");
}
static void pendulum_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1mp_set_string(&entity->model, "progs/pendulum.mdl");
  if (entity->spawnflags == 0 || isnan(entity->spawnflags)) entity->spawnflags = 2; /* TS ||= */
  if ((qa_js_to_int32(entity->spawnflags) & 3) == 0) qa_range_error("Unimplemented Pendulum Type (pendulum.qc)");
  const bool sideways = (qa_js_to_int32(entity->spawnflags) & 2) != 0;
  qa_q1_body_patch patch = {0};
  patch.has_angles = true; patch.angles = sideways ? QA_Q1_ZERO : QA_Q1MP_V3(0, 270, 0);
  patch.has_bounds = true;
  patch.bounds = sideways ? (qa_bounds){{-8, -24, -100}, {8, 24, 100}} : (qa_bounds){{-24, -8, -100}, {24, 8, 100}};
  qa_q1_set_body(game, entity, &patch);
  if (qa_q1_actor_number(entity, "currentammo") == 0) qa_q1mp_world_number(entity, "currentammo", 5);
  if (entity->delay == 0 || isnan(entity->delay)) entity->delay = 1;
  entity->solid = "trigger"; qa_q1_actor_set_damageable(entity, false);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "rogue:pend_touch");
  if (game->world != NULL) qa_q1mp_world_number(game->world, "rogue:impactVelocity", 0);
  if ((qa_js_to_int32(entity->spawnflags) & 8) != 0) entity->use = qa_q1_callback_registry_use(game->named, entity, "rogue:pend_use");
  else qa_q1mp_world_later(game, entity, entity->delay, "rogue:pend_swing1");
}
void qa_q1mp_register_rogue_pendulum(qa_q1_services *game) {
  for (size_t i = 0; i < SWING_COUNT; i++)
    qa_q1_callback_registry_register(game->named, SWING_NAMES[i],
                                     (qa_q1_callback_handlers){.ctx = (void *)SWINGS[i], .action = swing});
  qa_q1_callback_registry_register(game->named, "rogue:pend_touch", (qa_q1_callback_handlers){.touch = pend_touch});
  qa_q1_callback_registry_register(game->named, "rogue:pend_use", (qa_q1_callback_handlers){.use = pend_use});
  qa_q1_register_spawn(game, "pendulum", pendulum_spawn, NULL);
}
