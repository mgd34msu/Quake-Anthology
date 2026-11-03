/* Port of src/content/q1/missionpacks/presentation.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/presentation.h"

#include "missionpacks_internal.h"

static const qa_q1_character_definition HAMMER = {"progs/playham.mdl", {6, 12}, {0, 6}, {18, 6}, {24, 8}};

qa_q1_character_source_pose qa_q1mp_mission_pack_character_pose(qa_q1_services *game, qa_actor_id actor, qa_q1mp_pack pack) {
  qa_q1_character_source_pose pose;
  memset(&pose, 0, sizeof pose); /* { frame: null } */
  const qa_q1_player_state *player = qa_q1_player(game, actor);
  if (player == NULL) return pose;
  const double elapsed = player->weapon_animation_at < 0 ? -1 : floor((game->time - player->weapon_animation_at) / 0.1);
  const bool attacking = elapsed >= 0 && elapsed < 6;
  if (qa_q1mp_streq(pack, "hipnotic") && qa_q1mp_streq(player->weapon, "hipnotic:mjolnir")) {
    pose.definition = &HAMMER;
    if (attacking) { pose.has_frame = true; pose.frame = player->weapon_animation_base + elapsed; }
    return pose;
  }
  if (player->continuous_firing) {
    if (qa_q1mp_streq(player->weapon, "hipnotic:laser")) {
      pose.has_frame = true; pose.frame = player->weapon_frame == 1 ? 103 : 104;
      return pose;
    }
    if (qa_q1mp_streq(player->weapon, "rogue:lava-nailgun") || qa_q1mp_streq(player->weapon, "rogue:lava-supernailgun")) {
      pose.has_frame = true; pose.frame = fmod(player->weapon_frame, 2) == 1 ? 103 : 104;
      return pose;
    }
  }
  if (attacking && (qa_q1mp_streq(player->weapon, "hipnotic:proximity") || qa_q1mp_streq(player->weapon, "rogue:multi-grenade") ||
                    qa_q1mp_streq(player->weapon, "rogue:multi-rocket"))) {
    pose.has_frame = true; pose.frame = 107 + elapsed;
  }
  return pose;
}

static void head_flies(void *ctx, qa_q1_services *runtime, qa_q1_actor *timer) {
  (void)ctx;
  qa_owned_actor owner;
  const bool has_owner = timer->has_owner && qa_session_actor_registry_resolve_owned(runtime->host.actors, timer->owner, &owner);
  if (has_owner && qa_q1_health(runtime, owner.id) <= 0 &&
      !(runtime->world != NULL && qa_q1_actor_number(runtime->world, "worldtype") == 2) &&
      runtime->host.random(runtime->host.ctx) < 0.1) {
    qa_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = "sound"; event.has_actor = true; event.actor = owner.id; event.path = "misc/flys.wav";
    event.sound_channel = "6"; event.attenuation = 3; event.volume = 0.7;
    qa_q1_emit(runtime, event);
  }
  qa_q1_remove(runtime, timer);
}
void qa_q1mp_character_effects_init(qa_q1mp_mission_pack_character_effects *effects, qa_q1_services *game, qa_q1mp_pack pack,
                                    bool (*footsteps)(void *ctx), void *footsteps_ctx) {
  effects->game = game; effects->pack = pack; effects->footsteps = footsteps; effects->footsteps_ctx = footsteps_ctx;
  if (qa_q1mp_streq(pack, "hipnotic"))
    qa_q1_callback_registry_register(game->named, "hipnotic:head-flies", (qa_q1_callback_handlers){.action = head_flies});
}
static qa_q1_actor *character_state(qa_q1mp_mission_pack_character_effects *effects, qa_actor_id actor) {
  qa_q1_services *game = effects->game;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (qa_q1mp_streq(entity->classname, "missionpack_character_state") && entity->has_owner && qa_same_actor(entity->owner, actor)) return entity;
  }
  qa_q1_actor *entity = qa_q1_create(game, "missionpack_character_state", NULL, NULL);
  entity->has_owner = true; entity->owner = actor;
  return entity;
}
void qa_q1mp_character_effects_frame(qa_q1mp_mission_pack_character_effects *effects, qa_actor_id actor,
                                     const qa_q1_character_presentation *presentation) {
  if (!qa_q1mp_streq(effects->pack, "hipnotic")) return;
  qa_q1_services *game = effects->game;
  qa_q1_actor *state = character_state(effects, actor);
  const qa_body_state_result read = qa_shared_body_table_read(game->host.bodies, actor);
  if (!read.has_value) return;
  const qa_body_state body = read.value;
  if (qa_q1mp_streq(presentation->model, "progs/h_player.mdl") && !qa_q1mp_streq(qa_q1_actor_text(state, "model"), presentation->model)) {
    qa_q1_actor *timer = qa_q1_create(game, "hipnotic_head_flies", NULL, NULL);
    timer->has_owner = true; timer->owner = actor;
    qa_q1_schedule(game, timer, 1.5, qa_q1_callback_registry_action(game->named, timer, "hipnotic:head-flies"));
  }
  qa_q1mp_field_text(state, "model", presentation->model);
  const qa_q1_player_state *player = qa_q1_player(game, actor);
  const bool locomotion = qa_q1mp_streq(presentation->model, "progs/playham.mdl") ? presentation->frame < 18 : presentation->frame < 29;
  const bool footsteps = effects->footsteps != NULL && effects->footsteps(effects->footsteps_ctx);
  if (!footsteps || !qa_q1mp_streq(presentation->life, "alive") || !locomotion || (body.velocity.x == 0 && body.velocity.y == 0) ||
      game->time < qa_q1_actor_number(state, "next-step") || player == NULL || player->weapon_animation_at != -1 || player->continuous_firing)
    return;
  qa_q1mp_field_number(state, "next-step", qa_q1mp_fround(game->time + 0.1));
  double left = qa_q1_actor_number(state, "distance");
  double right = qa_q1_length(qa_q1_vsub(body.origin, qa_q1_actor_vector(state, "old-origin")));
  double distance = qa_q1mp_fround(left + right);
  char origin[128];
  (void)snprintf(origin, sizeof origin, "%s %s %s", QA_JS_NUMBER(body.origin.x), QA_JS_NUMBER(body.origin.y), QA_JS_NUMBER(body.origin.z));
  qa_q1mp_field_text(state, "old-origin", origin);
  if (body.ground != NULL && distance > 95) {
    distance = distance > 190 ? 0 : qa_q1mp_fround(0.5 * (distance - 95));
    const double roll = game->host.random(game->host.ctx);
    const int step = roll < 0.14 ? 1 : roll < 0.29 ? 2 : roll < 0.43 ? 3 : roll < 0.58 ? 4 : roll < 0.72 ? 5 : roll < 0.86 ? 6 : 7;
    char path[32];
    (void)snprintf(path, sizeof path, "misc/foot%d.wav", step);
    qa_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = "sound"; event.has_actor = true; event.actor = actor; event.path = path;
    event.sound_channel = "voice"; event.attenuation = 1; event.volume = 0.5;
    qa_q1_emit(game, event);
  }
  qa_q1mp_field_number(state, "distance", distance);
}
