/* Port of src/content/q1/missionpacks/presentation.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/presentation.h"
#include "missionpacks_internal.h"
#include <math.h>
#include <stdio.h>

static const qa_q1mp_character_definition HAMMER = {"progs/playham.mdl", {6,12}, {0,6}, {18,6}, {24,8}};
static qa_q1mp_character_source_pose pose_frame(bool has, double f) {
  qa_q1mp_character_source_pose p = {0}; p.has_frame = has; p.frame = f; return p;
}
qa_q1mp_character_source_pose qa_q1mp_mission_pack_character_pose(qa_q1mp_game *game, qa_actor_id actor, qa_q1mp_pack pack) {
  qa_q1mp_player_state *player = game->base->player(game->base, actor);
  if (!player) return pose_frame(false, 0);
  double elapsed = player->weapon_animation_at < 0 ? -1 : floor((game->base->time - player->weapon_animation_at) / 0.1);
  bool attacking = elapsed >= 0 && elapsed < 6;
  if (qa_q1mp_streq(pack, "hipnotic") && qa_q1mp_streq(player->weapon, "hipnotic:mjolnir")) {
    qa_q1mp_character_source_pose p = pose_frame(attacking, player->weapon_animation_base + elapsed);
    p.has_definition = true; p.definition = HAMMER; if (!attacking) p.has_frame = false; return p;
  }
  if (player->continuous_firing) {
    if (qa_q1mp_streq(player->weapon, "hipnotic:laser")) return pose_frame(true, player->weapon_frame == 1 ? 103 : 104);
    if (qa_q1mp_streq(player->weapon, "rogue:lava-nailgun") || qa_q1mp_streq(player->weapon, "rogue:lava-supernailgun"))
      return pose_frame(true, (int)player->weapon_frame % 2 == 1 ? 103 : 104);
  }
  if (attacking && (qa_q1mp_streq(player->weapon,"hipnotic:proximity")||qa_q1mp_streq(player->weapon,"rogue:multi-grenade")||qa_q1mp_streq(player->weapon,"rogue:multi-rocket")))
    return pose_frame(true, 107 + elapsed);
  return pose_frame(false, 0);
}
static void head_flies(void *ctx, qa_q1b_game *runtime, qa_q1b_actor *timer) {
  (void)ctx;
  qa_owned_actor owner; bool has = timer->owner.has && runtime->host->actors_resolve_owned(runtime->host->ctx, timer->owner.id, &owner);
  if (has && runtime->health(runtime, owner.id) <= 0) {
    double wt = runtime->world ? qa_q1b_actor_number(runtime->world, "worldtype") : 0;
    bool has_wt = runtime->world && qa_q1b_fields_get(runtime->world, "worldtype") != NULL;
    if ((!has_wt || wt != 2) && runtime->host->random(runtime->host->ctx) < 0.1) {
      qa_q1b_event ev = {0}; ev.kind = "sound"; ev.actor = QA_Q1MP_SOME(owner.id); ev.path = "misc/flys.wav";
      /* channel 6 / attenuation 3 / volume 0.7 carried in q1b event flat fields where present */
      ev.attenuation = 3; ev.volume = 0.7; runtime->host->emit(runtime->host->ctx, &ev);
    }
  }
  runtime->remove(runtime, timer);
}
void qa_q1mp_character_effects_init(qa_q1mp_mission_pack_character_effects *effects, qa_q1mp_game *game, qa_q1mp_pack pack, bool (*footsteps)(void *ctx), void *footsteps_ctx) {
  effects->game = game; effects->pack = pack; effects->footsteps = footsteps; effects->footsteps_ctx = footsteps_ctx;
  if (qa_q1mp_streq(pack, "hipnotic")) {
    qa_q1b_handlers h = {0}; h.ctx = effects; h.action = head_flies;
    game->base->named.register_named(game->base->named.ctx, "hipnotic:head-flies", &h);
  }
}
static qa_q1mp_actor *state_of(qa_q1mp_mission_pack_character_effects *fx, qa_actor_id actor) {
  qa_q1mp_actor *ents[256]; qa_q1b_game *g = fx->game->base;
  size_t n = g->entities(g, ents, 256);
  for (size_t i = 0; i < n; i++) if (qa_q1mp_streq(ents[i]->classname, "missionpack_character_state") && ents[i]->owner.has && qa_same_actor(ents[i]->owner.id, actor)) return ents[i];
  qa_q1mp_actor *e = g->create(g, "missionpack_character_state"); e->owner = QA_Q1MP_SOME(actor); return e;
}
void qa_q1mp_character_effects_frame(qa_q1mp_mission_pack_character_effects *fx, qa_actor_id actor, const qa_q1mp_character_presentation *presentation) {
  if (!qa_q1mp_streq(fx->pack, "hipnotic")) return;
  qa_q1mp_game *game = fx->game; qa_q1b_game *g = game->base;
  qa_q1mp_actor *state = state_of(fx, actor); qa_q1b_body body;
  if (!qa_q1mp_body_read(game, actor, &body)) return;
  if (qa_q1mp_streq(presentation->model, "progs/h_player.mdl") && !qa_q1mp_streq(qa_q1mp_text(state, "model"), presentation->model)) {
    qa_q1mp_actor *timer = g->create(g, "hipnotic_head_flies"); timer->owner = QA_Q1MP_SOME(actor);
    g->schedule(g, timer, 1.5, g->named.action(g->named.ctx, timer, "hipnotic:head-flies"));
  }
  qa_q1b_fields_set(state, "model", presentation->model);
  qa_q1mp_player_state *player = g->player(g, actor);
  bool locomotion = qa_q1mp_streq(presentation->model, "progs/playham.mdl") ? presentation->frame < 18 : presentation->frame < 29;
  bool footsteps = fx->footsteps ? fx->footsteps(fx->footsteps_ctx) : false;
  if (!footsteps || !qa_q1mp_streq(presentation->life, "alive") || !locomotion || (body.velocity.x == 0 && body.velocity.y == 0) || g->time < qa_q1mp_number(state, "next-step") || !player || player->weapon_animation_at != -1 || player->continuous_firing) return;
  char buf[256]; qa_q1b_format_fround(buf, 32, g->time + 0.1); qa_q1b_fields_set(state, "next-step", buf);
  double distance = qa_q1mp_fround(qa_q1mp_number(state, "distance") + qa_q1mp_length(qa_q1mp_vsub(body.origin, qa_q1mp_vector_of(state, "old-origin"))));
  (void)snprintf(buf, sizeof buf, "%g %g %g", body.origin.x, body.origin.y, body.origin.z); qa_q1b_fields_set(state, "old-origin", buf);
  if (body.ground.has && distance > 95) {
    distance = distance > 190 ? 0 : qa_q1mp_fround(0.5 * (distance - 95));
    double roll = qa_q1mp_random(game);
    int step = roll < 0.14 ? 1 : roll < 0.29 ? 2 : roll < 0.43 ? 3 : roll < 0.58 ? 4 : roll < 0.72 ? 5 : roll < 0.86 ? 6 : 7;
    char path[64]; (void)snprintf(path, sizeof path, "misc/foot%d.wav", step);
    if (game->emit_sound_event) game->emit_sound_event(game->ctx, actor, path, QA_Q1MP_CH("voice"), 0.5, 1);
    else { qa_q1b_event ev = {0}; ev.kind = "sound"; ev.actor = QA_Q1MP_SOME(actor); ev.path = path; ev.volume = 0.5; ev.attenuation = 1; g->host->emit(g->host->ctx, &ev); }
  }
  qa_q1b_format_number(buf, sizeof buf, distance); qa_q1b_fields_set(state, "distance", buf);
}
