/* Port of src/bots/behavior/rerelease/profile.ts. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/profile.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>

static char *dups(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p;
}
const char *qa_rerelease_source_name(qa_rerelease_source source) {
  return source == QA_RERELEASE_Q2 ? "q2-rerelease" : "q1-rerelease";
}
bool qa_rerelease_source_from_name(const char *name, qa_rerelease_source *out) {
  if (!name) return false;
  if (!strcmp(name, "q1-rerelease")) { *out = QA_RERELEASE_Q1; return true; }
  if (!strcmp(name, "q2-rerelease")) { *out = QA_RERELEASE_Q2; return true; }
  return false;
}
static void pending_clear(qa_rerelease_pending_chat *v, size_t n) {
  for (size_t i = 0; i < n; i++) { free(v[i].locstring); free(v[i].type); }
  free(v);
}
static void on_chat_adaptor(void *ctx, qa_bot_chat_event event) {
  qa_rerelease_bot_behavior *b = ctx;
  if (b->pending_count == b->pending_cap) {
    size_t c = b->pending_cap ? b->pending_cap * 2 : 8;
    qa_rerelease_pending_chat *p = realloc(b->pending_chats, c * sizeof *p);
    if (!p) qa_range_error("Out of memory");
    b->pending_chats = p; b->pending_cap = c;
  }
  qa_rerelease_pending_chat *entry = &b->pending_chats[b->pending_count];
  entry->locstring = dups(event.locstring ? event.locstring : "");
  entry->type = dups(event.type ? event.type : "");
  if (!entry->locstring || !entry->type) qa_range_error("Out of memory");
  entry->time = b->callbacks.time(b->callbacks.ctx) + event.delay_ms / 1000;
  entry->delay_ms = event.delay_ms;
  entry->team_only = event.team_only;
  b->pending_count++;
}
static bool near_adaptor(void *ctx) {
  qa_rerelease_bot_behavior *b = ctx;
  return b->callbacks.human_teammate_near(b->callbacks.ctx);
}
static double impulse_adaptor(void *ctx, double weapon_number) {
  qa_rerelease_bot_behavior *b = ctx;
  return b->callbacks.weapon_impulse(b->callbacks.ctx, weapon_number);
}
static void select_adaptor(void *ctx, double weapon_number) {
  qa_rerelease_bot_behavior *b = ctx;
  b->callbacks.select_weapon(b->callbacks.ctx, weapon_number);
}
bool qa_rerelease_bot_behavior_init(qa_rerelease_bot_behavior *behavior, qa_rerelease_profile_options options) {
  memset(behavior, 0, sizeof *behavior);
  behavior->definition = dups(options.definition ? options.definition : "");
  if (!behavior->definition) return false;
  behavior->source = options.source;
  behavior->callbacks = options.callbacks;
  qa_xorshift32_init(&behavior->random, options.seed);
  qa_bot_brain_config config; memset(&config, 0, sizeof config);
  config.knowledge = options.knowledge;
  config.skill = options.skill;
  config.rng = qa_xorshift32_as_random(&behavior->random);
  config.game_mode = options.game_mode;
  config.character = options.character;
  config.has_max_health = true; config.max_health = options.max_health;
  config.has_run_speed = true; config.run_speed = options.run_speed;
  config.has_walk_speed = true; config.walk_speed = options.walk_speed;
  config.has_movement = true; config.movement = options.movement;
  config.on_chat_ctx = behavior; config.on_chat = on_chat_adaptor;
  config.human_teammate_near_ctx = behavior; config.human_teammate_near = near_adaptor;
  if (options.source == QA_RERELEASE_Q1) {
    config.weapon_impulse_ctx = behavior; config.weapon_impulse = impulse_adaptor;
  } else {
    config.on_weapon_select_ctx = behavior; config.on_weapon_select = select_adaptor;
  }
  if (!qa_bot_brain_init(&behavior->brain, config)) {
    free(behavior->definition); behavior->definition = NULL;
    return false;
  }
  return true;
}
void qa_rerelease_bot_behavior_destroy(qa_rerelease_bot_behavior *behavior) {
  if (!behavior) return;
  qa_bot_brain_destroy(&behavior->brain);
  pending_clear(behavior->pending_chats, behavior->pending_count);
  free(behavior->definition);
  memset(behavior, 0, sizeof *behavior);
}
static void orders_move(void *ctx, qa_bot_vec3 point) { qa_rerelease_bot_behavior_request_move_to_point(ctx, point); }
static void orders_follow(void *ctx, double entity, qa_bot_vec3 origin) { qa_rerelease_bot_behavior_request_follow_entity(ctx, entity, origin); }
static void orders_clear(void *ctx) { qa_rerelease_bot_behavior_clear_explicit_goal(ctx); }
static int64_t orders_status(void *ctx) { return qa_rerelease_bot_behavior_goal_status(ctx); }
qa_rerelease_orders qa_rerelease_bot_behavior_as_orders(qa_rerelease_bot_behavior *behavior) {
  return (qa_rerelease_orders){behavior, orders_move, orders_follow, orders_clear, orders_status};
}
qa_bot_usercmd qa_rerelease_bot_behavior_think(qa_rerelease_bot_behavior *behavior, qa_bot_world *world) {
  qa_rerelease_callbacks *cb = &behavior->callbacks;
  bool q1 = behavior->source == QA_RERELEASE_Q1;
  cb->pre_think(cb->ctx, q1 ? "Bot_PreThink" : "Bot_BeginFrame", qa_rerelease_bot_behavior_as_orders(behavior));
  qa_bot_usercmd command = qa_bot_brain_think(&behavior->brain, world);
  cb->post_think(cb->ctx, q1 ? "Bot_PostThink" : "Bot_EndFrame", command);
  double now = cb->time(cb->ctx);
  size_t kept = 0;
  for (size_t i = 0; i < behavior->pending_count; i++) {
    qa_rerelease_pending_chat entry = behavior->pending_chats[i];
    if (entry.time <= now) {
      qa_bot_chat_event event = {entry.locstring, entry.type, entry.delay_ms, entry.team_only};
      cb->chat(cb->ctx, event);
      free(entry.locstring); free(entry.type);
    } else {
      behavior->pending_chats[kept++] = entry;
    }
  }
  behavior->pending_count = kept;
  return command;
}
void qa_rerelease_bot_behavior_set_objective_goal(qa_rerelease_bot_behavior *behavior, const qa_bot_vec3 *point) {
  qa_bot_brain_set_objective_goal(&behavior->brain, point);
}
void qa_rerelease_bot_behavior_request_move_to_point(qa_rerelease_bot_behavior *behavior, qa_bot_vec3 point) {
  qa_bot_brain_request_move_to_point(&behavior->brain, point);
}
void qa_rerelease_bot_behavior_request_follow_entity(qa_rerelease_bot_behavior *behavior, double entity, qa_bot_vec3 origin) {
  qa_bot_brain_request_follow_entity(&behavior->brain, entity, origin);
}
void qa_rerelease_bot_behavior_clear_explicit_goal(qa_rerelease_bot_behavior *behavior) {
  qa_bot_brain_clear_explicit_goal(&behavior->brain);
}
int64_t qa_rerelease_bot_behavior_goal_status(qa_rerelease_bot_behavior *behavior) {
  return qa_bot_brain_goal_status(&behavior->brain);
}
bool qa_rerelease_bot_behavior_reset_for_level(qa_rerelease_bot_behavior *behavior, qa_bot_game_mode mode) {
  qa_bot_brain_reset_for_level(&behavior->brain);
  if (!qa_bot_brain_set_game_mode(&behavior->brain, mode)) return false;
  pending_clear(behavior->pending_chats, behavior->pending_count);
  behavior->pending_chats = NULL; behavior->pending_count = behavior->pending_cap = 0;
  return true;
}
void qa_rerelease_behavior_checkpoint_destroy(qa_rerelease_behavior_checkpoint *checkpoint) {
  if (!checkpoint) return;
  free(checkpoint->definition);
  qa_bot_brain_checkpoint_destroy(&checkpoint->brain);
  pending_clear(checkpoint->pending_chats, checkpoint->pending_count);
  memset(checkpoint, 0, sizeof *checkpoint);
}
bool qa_rerelease_bot_behavior_checkpoint_make(qa_rerelease_bot_behavior *behavior, qa_rerelease_behavior_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->version = 1;
  out->source = behavior->source;
  out->definition = dups(behavior->definition ? behavior->definition : "");
  if (!out->definition) goto oom;
  out->rng = qa_xorshift32_peek(&behavior->random);
  if (!qa_bot_brain_checkpoint_make(&behavior->brain, &out->brain)) goto oom;
  out->pending_count = behavior->pending_count;
  if (out->pending_count) {
    out->pending_chats = calloc(out->pending_count, sizeof *out->pending_chats);
    if (!out->pending_chats) goto oom;
    for (size_t i = 0; i < out->pending_count; i++) {
      out->pending_chats[i] = behavior->pending_chats[i];
      out->pending_chats[i].locstring = dups(behavior->pending_chats[i].locstring ? behavior->pending_chats[i].locstring : "");
      out->pending_chats[i].type = dups(behavior->pending_chats[i].type ? behavior->pending_chats[i].type : "");
      if (!out->pending_chats[i].locstring || !out->pending_chats[i].type) goto oom;
    }
  }
  return true;
oom:
  qa_rerelease_behavior_checkpoint_destroy(out);
  return false;
}
bool qa_rerelease_bot_behavior_restore(qa_rerelease_bot_behavior *behavior, const qa_rerelease_behavior_checkpoint *checkpoint) {
  if (checkpoint->version != 1 || checkpoint->source != behavior->source ||
      !checkpoint->definition || !behavior->definition || strcmp(checkpoint->definition, behavior->definition))
    qa_range_error("Rerelease behavior checkpoint belongs to another mounted source definition");
  if (!qa_bot_brain_restore(&behavior->brain, &checkpoint->brain)) return false;
  qa_xorshift32_restore(&behavior->random, checkpoint->rng);
  qa_rerelease_pending_chat *fresh = NULL;
  if (checkpoint->pending_count) {
    fresh = calloc(checkpoint->pending_count, sizeof *fresh);
    if (!fresh) return false;
    for (size_t i = 0; i < checkpoint->pending_count; i++) {
      fresh[i] = checkpoint->pending_chats[i];
      fresh[i].locstring = dups(checkpoint->pending_chats[i].locstring ? checkpoint->pending_chats[i].locstring : "");
      fresh[i].type = dups(checkpoint->pending_chats[i].type ? checkpoint->pending_chats[i].type : "");
      if (!fresh[i].locstring || !fresh[i].type) { pending_clear(fresh, checkpoint->pending_count); return false; }
    }
  }
  pending_clear(behavior->pending_chats, behavior->pending_count);
  behavior->pending_chats = fresh;
  behavior->pending_count = behavior->pending_cap = checkpoint->pending_count;
  return true;
}
