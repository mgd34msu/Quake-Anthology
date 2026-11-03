/* Port of src/content/q2/multiplayer/ctf/native-grapple-hooks.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/ctf/native_grapple_hooks.h"
#include "../multiplayer_internal.h"

/* TS Q2WeaponInput hand literal. */
static const char *hand_name(qa_q2w_hand hand) {
  return hand == QA_Q2W_HAND_LEFT ? "left" : hand == QA_Q2W_HAND_CENTER ? "center" : "right";
}

static qa_grapple_pose pose(void *ctxv, qa_actor_id actor, qa_q2_game_services *g) {
  qa_q2_ctf_native_grapple_ctx *ctx = ctxv;
  qa_q2_entity *entity = qa_q2_game_entity_of(g, &actor);
  if (entity == NULL) qa_raise("Error", 0, 0, "Native grapple binding requires its source player entity");
  const qa_q2w_input *input = ctx->weapon_aim ? qa_q2w_ballistics_input(&ctx->hooks->weapons->base, actor) : NULL;
  qa_q2_player_view_result view = g->host->player_view_state(g->host->ctx, actor);
  qa_q2_base_player_state *player = ctx->hooks->player(ctx->hooks->ctx, actor);
  qa_grapple_pose out = {0};
  out.angles = input != NULL ? input->angles : view.has_value ? view.value.view_angles : qa_q2_game_body(g, entity).angles;
  out.hand = input != NULL ? hand_name(input->hand) : player != NULL ? player->hand : "right";
  out.view_height = entity->view_height;
  out.gravity = entity->gravity;
  out.gravity_vector = entity->gravity_vector;
  return out;
}

static bool starts_with(const char *s, const char *prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

static const char *anchor(void *ctxv, qa_actor_id actor, qa_q2_game_services *g) {
  qa_q2_ctf_native_grapple_ctx *ctx = ctxv;
  if (qa_same_actor(actor, g->host->world_actor(g->host->ctx))) return "world";
  qa_q2_entity *entity = qa_q2_game_entity_of(g, &actor);
  if (entity != NULL && strcmp(entity->classname, "bodyque") == 0) return "corpse";
  if (!ctx->weapon_aim && entity != NULL && (starts_with(entity->classname, "info_flag") || starts_with(entity->classname, "func"))) return "brush";
  if (entity != NULL && strcmp(entity->solid, "none") == 0) return "none";
  if (g->host->is_player(g->host->ctx, actor)) return "player";
  return entity != NULL && strcmp(entity->solid, "box") == 0 ? "box"
         : entity != NULL && strcmp(entity->solid, "brush") == 0 ? "brush" : "none";
}

static bool dead(void *ctxv, qa_actor_id actor, qa_q2_game_services *g) {
  qa_q2_ctf_native_grapple_ctx *ctx = ctxv;
  qa_combat_state_result combat = qa_q2mp_combat_read(g, actor);
  qa_q2_base_player_state *player = ctx->hooks->player(ctx->hooks->ctx, actor);
  return (player != NULL && player->dead) || (combat.has_value && combat.value.can_take_damage && combat.value.health <= 0);
}

static qa_vec3 previous_velocity(void *ctxv, qa_actor_id actor) {
  qa_q2_ctf_native_grapple_ctx *ctx = ctxv;
  qa_q2_base_player_state *player = ctx->hooks->player(ctx->hooks->ctx, actor);
  return player == NULL ? QA_Q2_ZERO : player->old_velocity;
}

static void set_previous_velocity(void *ctxv, qa_actor_id actor, qa_vec3 velocity) {
  qa_q2_ctf_native_grapple_ctx *ctx = ctxv;
  qa_q2_base_player_state *player = ctx->hooks->player(ctx->hooks->ctx, actor);
  if (player != NULL) player->old_velocity = velocity;
}

static double volume(void *ctxv, qa_actor_id actor) {
  qa_q2_ctf_native_grapple_ctx *ctx = ctxv;
  return qa_q2w_ballistics_silencer_shots(&ctx->hooks->weapons->base, actor) > 0 ? 0.2 : 1;
}

static void noise(void *ctxv, qa_actor_id actor, qa_q2_game_services *g, qa_vec3 origin, const char *kind) {
  qa_q2_ctf_native_grapple_ctx *ctx = ctxv;
  qa_q2w_ballistics_player_noise_for_actor(&ctx->hooks->weapons->base, actor, g, origin, kind);
}

static void set_prediction(void *ctxv, qa_actor_id actor, bool suppressed) {
  qa_q2_ctf_native_grapple_ctx *ctx = ctxv;
  ctx->hooks->set_grapple_prediction(ctx->hooks->ctx, actor, suppressed);
}

static double gravity_fn(void *ctxv) {
  qa_q2_ctf_native_grapple_ctx *ctx = ctxv;
  return ctx->hooks->gravity(ctx->hooks->ctx);
}

static void emit_fn(void *ctxv, qa_grapple_cable_event event) {
  qa_q2_ctf_native_grapple_ctx *ctx = ctxv;
  ctx->emit_cable(ctx->emit_ctx, event);
}

qa_grapple_hooks qa_native_grapple_hooks(qa_q2_ctf_native_grapple_ctx *ctx) {
  qa_grapple_hooks hooks = {0};
  hooks.ctx = ctx;
  hooks.pose = pose;
  hooks.anchor = anchor;
  hooks.dead = dead;
  hooks.previous_velocity = previous_velocity;
  hooks.set_previous_velocity = set_previous_velocity;
  hooks.volume = volume;
  hooks.noise = noise;
  hooks.set_grapple_prediction = set_prediction;
  hooks.gravity = gravity_fn;
  hooks.emit = emit_fn;
  return hooks;
}
