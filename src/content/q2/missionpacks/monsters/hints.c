/* Port of src/content/q2/missionpacks/monsters/hints.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/hints.h"
#include "qa/network/common/js_number.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mp_internal.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/persistence/save_image.h"
#include "qa/foundation/js.h"

#define HINT_ENDPOINT 1
#define MAX_HINT_CHAINS 100
static const double HOLD_FOREVER = 9223372036854775807.0 / 1000.0;

static qa_q2mpm_hint_node *node_of(qa_q2mpm_hints *hints, qa_actor_id actor) {
  for (size_t i = 0; i < hints->node_count; i++)
    if (qa_same_actor(hints->nodes[i].actor, actor)) return &hints->nodes[i];
  return NULL;
}
static qa_q2mpm_hint_node *node_set(qa_q2mpm_hints *hints, qa_actor_id actor, double chain, bool has_next,
                                    qa_actor_id next) {
  qa_q2mpm_hint_node *existing = node_of(hints, actor);
  if (existing) {
    existing->chain = chain;
    existing->has_next = has_next;
    existing->next = next;
    return existing;
  }
  if (hints->node_count == hints->node_cap) {
    size_t cap = hints->node_cap ? hints->node_cap * 2 : 16;
    qa_q2mpm_hint_node *grown = realloc(hints->nodes, cap * sizeof *grown);
    if (!grown) qa_raise("Error", 0, 0, "Out of memory growing hint nodes");
    hints->nodes = grown;
    hints->node_cap = cap;
  }
  qa_q2mpm_hint_node *node = &hints->nodes[hints->node_count++];
  node->actor = actor;
  node->chain = chain;
  node->has_next = has_next;
  node->next = next;
  return node;
}
static void starts_push(qa_q2mpm_hints *hints, qa_actor_id actor) {
  if (hints->start_count == hints->start_cap) {
    size_t cap = hints->start_cap ? hints->start_cap * 2 : 16;
    qa_actor_id *grown = realloc(hints->starts, cap * sizeof *grown);
    if (!grown) qa_raise("Error", 0, 0, "Out of memory growing hint starts");
    hints->starts = grown;
    hints->start_cap = cap;
  }
  hints->starts[hints->start_count++] = actor;
}
static qa_q2mpm_hint_monster *monster_of(qa_q2mpm_hints *hints, qa_q2m_context *context) {
  for (size_t i = 0; i < hints->pursuer_count; i++)
    if (qa_same_actor(hints->pursuers[i].actor, context->entity->actor.id)) return &hints->pursuers[i];
  if (hints->pursuer_count == hints->pursuer_cap) {
    size_t cap = hints->pursuer_cap ? hints->pursuer_cap * 2 : 8;
    qa_q2mpm_hint_monster *grown = realloc(hints->pursuers, cap * sizeof *grown);
    if (!grown) qa_raise("Error", 0, 0, "Out of memory growing hint pursuers");
    hints->pursuers = grown;
    hints->pursuer_cap = cap;
  }
  qa_q2mpm_hint_monster *monster = &hints->pursuers[hints->pursuer_count++];
  memset(monster, 0, sizeof *monster);
  monster->actor = context->entity->actor.id;
  return monster;
}
static double source_slot(qa_q2_game_services *game, qa_q2_entity *entity) {
  qa_actor_source_result saved = qa_session_actor_registry_source_of(game->host->actors, entity->actor.id);
  return saved.has_value ? saved.slot : (double)entity->actor.id.slot;
}
static void touch_hook_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_touch_contact contact) {
  qa_q2mpm_hints *hints = ctx;
  qa_q2m_context *context = qa_q2m_monsters_context(hints->monsters, contact.other);
  if (context) qa_q2mpm_hints_touch(hints, entity, game, context);
}
static bool hooks_run(void *ctx, qa_q2m_context *context, double distance) {
  return qa_q2mpm_hints_run(ctx, context, distance);
}
static bool hooks_check_lost(void *ctx, qa_q2m_context *context) {
  return qa_q2mpm_hints_check_lost(ctx, context);
}
static void hooks_stop(void *ctx, qa_q2m_context *context) { qa_q2mpm_hints_stop(ctx, context); }

void qa_q2mpm_hints_init(qa_q2mpm_hints *hints, qa_q2m_monsters *monsters) {
  memset(hints, 0, sizeof *hints);
  hints->monsters = monsters;
  hints->hooks.ctx = hints;
  hints->hooks.run = hooks_run;
  hints->hooks.check_lost = hooks_check_lost;
  hints->hooks.stop = hooks_stop;
}
void qa_q2mpm_hints_destroy(qa_q2mpm_hints *hints) {
  free(hints->starts);
  free(hints->nodes);
  free(hints->pursuers);
  memset(hints, 0, sizeof *hints);
}
static qa_q2_named_touch hint_touch_entry(qa_q2mpm_hints *hints) {
  qa_q2_named_touch entry;
  entry.name = "hint_path_touch";
  entry.cb = (qa_q2_touch){hints, touch_hook_fn};
  return entry;
}
qa_q2_callback_definitions qa_q2mpm_hints_callbacks(qa_q2mpm_hints *hints) {
  /* The touch table borrows one static slot per hints object is not
   * possible; callers copy the returned definitions immediately. */
  static qa_q2_named_touch entries[1];
  entries[0] = hint_touch_entry(hints);
  qa_q2_callback_definitions definitions;
  memset(&definitions, 0, sizeof definitions);
  definitions.touch = entries;
  definitions.touch_count = 1;
  return definitions;
}
static qa_q2_callback_definitions spawn_callbacks_fn(void *ctx) { return qa_q2mpm_hints_callbacks(ctx); }
static bool spawn_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  return qa_q2mpm_hints_spawn(ctx, entity, game);
}
qa_q2_spawn_module qa_q2mpm_hints_as_spawn(qa_q2mpm_hints *hints) {
  hints->spawn_callbacks = spawn_callbacks_fn(hints);
  qa_q2_spawn_module module = {hints, &hints->spawn_callbacks, NULL, spawn_fn};
  return module;
}
static void position_text(qa_q2_entity *entity, qa_q2_game_services *game, char *buffer, size_t size) {
  qa_vec3 origin = q2mpm_body(game, entity).origin;
  snprintf(buffer, size, "%s %s %s", QA_JS_NUMBER(origin.x), QA_JS_NUMBER(origin.y), QA_JS_NUMBER(origin.z));
}
bool qa_q2mpm_hints_spawn(qa_q2mpm_hints *hints, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (strcmp(entity->classname, "hint_path") != 0) return false;
  if (game->options.mode == QA_Q2_MODE_DEATHMATCH) {
    game->remove(game->ctx, entity);
    return true;
  }
  const char *target = entity->target ? entity->target : "";
  const char *targetname = entity->targetname ? entity->targetname : "";
  if (target[0] == '\0' && targetname[0] == '\0') {
    char position[96];
    char message[192];
    position_text(entity, game, position, sizeof position);
    snprintf(message, sizeof message, "unlinked hint_path at %s", position);
    game->host->diagnostic(game->host->ctx, message);
    game->remove(game->ctx, entity);
    return true;
  }
  qa_actor_id none = entity->actor.id;
  (void)node_set(hints, entity->actor.id, -1, false, none);
  entity->touch = (qa_q2_touch){hints, touch_hook_fn};
  entity->server_flags = q2mpm_or(entity->server_flags, 1);
  entity->visible = false;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2mpm_v3(-8, -8, -8);
  change.bounds.max = q2mpm_v3(8, 8, 8);
  q2mpm_move_body(game, entity, change, true, false);
  game->solid(game->ctx, entity, "trigger");
  game->show(game->ctx, entity);
  return true;
}
void qa_q2mpm_hint_init_free(qa_q2mpm_hint_initialization *report) {
  for (size_t i = 0; i < report->issue_count; i++) free(report->issues[i]);
  free(report->issues);
  memset(report, 0, sizeof *report);
}
static char *duplicate_text(const char *text) {
  char *copy = malloc(strlen(text) + 1);
  if (!copy) qa_raise("Error", 0, 0, "Out of memory recording hint issue");
  strcpy(copy, text);
  return copy;
}
static int compare_entities(const void *left, const void *right) {
  const double *slots = NULL;
  (void)slots;
  qa_q2_entity *const *a = left;
  qa_q2_entity *const *b = right;
  double sa = (double)(*a)->actor.id.slot;
  double sb = (double)(*b)->actor.id.slot;
  return sa < sb ? -1 : sa > sb ? 1 : 0;
}
typedef struct sort_entry {
  qa_q2_entity *entity;
  double slot;
} sort_entry;
static int compare_slots(const void *left, const void *right) {
  const sort_entry *a = left;
  const sort_entry *b = right;
  return a->slot < b->slot ? -1 : a->slot > b->slot ? 1 : 0;
}
bool qa_q2mpm_hints_finalize(qa_q2mpm_hints *hints, qa_q2_game_services *game, qa_q2mpm_hint_initialization *out) {
  memset(out, 0, sizeof *out);
  hints->start_count = 0;
  hints->node_count = 0;
  size_t hint_count = 0;
  for (size_t i = 0; i < qa_q2_game_entity_count(game); i++)
    if (strcmp(qa_q2_game_entity_at(game, i)->classname, "hint_path") == 0) hint_count++;
  sort_entry *sorted = malloc((hint_count ? hint_count : 1) * sizeof *sorted);
  if (!sorted) return false;
  size_t at = 0;
  for (size_t i = 0; i < qa_q2_game_entity_count(game); i++) {
    if (strcmp(qa_q2_game_entity_at(game, i)->classname, "hint_path") != 0) continue;
    sorted[at].entity = qa_q2_game_entity_at(game, i);
    sorted[at].slot = source_slot(game, qa_q2_game_entity_at(game, i));
    at++;
  }
  qa_js_sort(sorted, hint_count, sizeof *sorted, compare_slots);
  (void)compare_entities;
  hints->present = hint_count != 0;
  size_t issue_cap = 0;
  for (size_t i = 0; i < hint_count; i++) {
    qa_q2_entity *hint = sorted[i].entity;
    (void)node_set(hints, hint->actor.id, -1, false, hint->actor.id);
    const char *target = hint->target ? hint->target : "";
    const char *targetname = hint->targetname ? hint->targetname : "";
    if ((qa_js_to_uint32(hint->spawnflags) & HINT_ENDPOINT) == 0 || target[0] == '\0') continue;
    if (targetname[0] != '\0') {
      char position[96];
      char message[512];
      position_text(hint, game, position, sizeof position);
      snprintf(message, sizeof message,
               "Hint path at %s marked as endpoint with both target (%s) and targetname (%s)",
               position, target, targetname);
      if (out->issue_count == issue_cap) {
        issue_cap = issue_cap ? issue_cap * 2 : 4;
        char **grown = realloc(out->issues, issue_cap * sizeof *grown);
        if (!grown) { free(sorted); qa_q2mpm_hint_init_free(out); return false; }
        out->issues = grown;
      }
      out->issues[out->issue_count++] = duplicate_text(message);
      game->host->diagnostic(game->host->ctx, message);
    } else if (hints->start_count < MAX_HINT_CHAINS) {
      starts_push(hints, hint->actor.id);
    }
  }
  for (size_t chain = 0; chain < hints->start_count; chain++) {
    qa_actor_id actor = hints->starts[chain];
    qa_q2_entity *start = game->entity(game->ctx, &actor);
    if (!start) continue;
    qa_q2mpm_hint_node *root = node_set(hints, actor, (double)chain, false, actor);
    qa_q2_entity *current = start;
    size_t visited_count = 0;
    size_t visited_cap = 16;
    qa_actor_id *visited = malloc(visited_cap * sizeof *visited);
    if (!visited) { free(sorted); qa_q2mpm_hint_init_free(out); return false; }
    visited[visited_count++] = actor;
    const char *current_target = current->target ? current->target : "";
    while (current_target[0] != '\0') {
      size_t match_count = 0;
      qa_q2_entity *const *matches = game->targets(game->ctx, current_target, &match_count);
      qa_q2_entity *next = match_count ? matches[0] : NULL;
      if (match_count > 1) {
        char position[96];
        char message[512];
        position_text(current, game, position, sizeof position);
        snprintf(message, sizeof message, "Forked hint path at %s detected for chain %zu, target %s",
                 position, hints->start_count, current_target);
        if (out->issue_count == issue_cap) {
          issue_cap = issue_cap ? issue_cap * 2 : 4;
          char **grown = realloc(out->issues, issue_cap * sizeof *grown);
          if (!grown) { free(visited); free(sorted); qa_q2mpm_hint_init_free(out); return false; }
          out->issues = grown;
        }
        out->issues[out->issue_count++] = duplicate_text(message);
        game->host->diagnostic(game->host->ctx, message);
        root->has_next = false;
        break;
      }
      if (!next) break;
      bool seen = false;
      for (size_t v = 0; v < visited_count; v++) if (qa_same_actor(visited[v], next->actor.id)) seen = true;
      qa_q2mpm_hint_node *previous = node_of(hints, next->actor.id);
      if (seen || (previous && previous->has_next)) {
        char position[96];
        char message[512];
        position_text(next, game, position, sizeof position);
        snprintf(message, sizeof message, "Circular hint path at %s detected for chain %zu, targetname %s",
                 position, hints->start_count, next->targetname ? next->targetname : "");
        if (out->issue_count == issue_cap) {
          issue_cap = issue_cap ? issue_cap * 2 : 4;
          char **grown = realloc(out->issues, issue_cap * sizeof *grown);
          if (!grown) { free(visited); free(sorted); qa_q2mpm_hint_init_free(out); return false; }
          out->issues = grown;
        }
        out->issues[out->issue_count++] = duplicate_text(message);
        game->host->diagnostic(game->host->ctx, message);
        root->has_next = false;
        break;
      }
      qa_q2mpm_hint_node *current_node = node_of(hints, current->actor.id);
      if (!current_node) {
        free(visited);
        free(sorted);
        qa_q2mpm_hint_init_free(out);
        qa_raise("Error", 0, 0, "Hint chain lost its current node during initialization");
      }
      current_node->has_next = true;
      current_node->next = next->actor.id;
      (void)node_set(hints, next->actor.id, (double)chain, false, next->actor.id);
      root = node_of(hints, actor);
      if (visited_count == visited_cap) {
        visited_cap *= 2;
        qa_actor_id *grown = realloc(visited, visited_cap * sizeof *grown);
        if (!grown) { free(visited); free(sorted); qa_q2mpm_hint_init_free(out); return false; }
        visited = grown;
      }
      visited[visited_count++] = next->actor.id;
      current = next;
      current_target = current->target ? current->target : "";
    }
    free(visited);
  }
  free(sorted);
  out->present = hints->present;
  out->chains = hints->start_count;
  return true;
}
static size_t chain_collect(qa_q2mpm_hints *hints, qa_q2_game_services *game, qa_actor_id start, qa_q2_entity **buffer,
                            size_t capacity) {
  size_t count = 0;
  bool has_actor = true;
  qa_actor_id actor = start;
  while (has_actor) {
    bool seen = false;
    for (size_t i = 0; i < count; i++) if (qa_same_actor(buffer[i]->actor.id, actor)) seen = true;
    if (seen) break;
    qa_q2_entity *entity = game->entity(game->ctx, &actor);
    if (!entity) break;
    if (count < capacity) buffer[count] = entity;
    count++;
    qa_q2mpm_hint_node *node = node_of(hints, actor);
    has_actor = node && node->has_next;
    if (has_actor) actor = node->next;
  }
  return count;
}
qa_q2_entity *qa_q2mpm_hints_find_start(qa_q2mpm_hints *hints, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)hints;
  const char *target = entity->target ? entity->target : "";
  bool forward = target[0] != '\0';
  size_t visited_cap = 16;
  size_t visited_count = 0;
  qa_actor_id *visited = malloc(visited_cap * sizeof *visited);
  if (!visited) qa_raise("Error", 0, 0, "Out of memory in hint findStart");
  visited[visited_count++] = entity->actor.id;
  qa_q2_entity *current = entity;
  qa_q2_entity *last = NULL;
  for (;;) {
    const char *name = forward ? (current->target ? current->target : "")
                               : (current->targetname ? current->targetname : "");
    if (name[0] == '\0') break;
    qa_q2_entity *next = NULL;
    if (forward) {
      size_t match_count = 0;
      qa_q2_entity *const *matches = game->targets(game->ctx, name, &match_count);
      next = match_count ? matches[0] : NULL;
    } else {
      double best_slot = 0;
      bool have = false;
      for (size_t i = 0; i < qa_q2_game_entity_count(game); i++) {
        qa_q2_entity *candidate = qa_q2_game_entity_at(game, i);
        const char *candidate_target = candidate->target ? candidate->target : "";
        if (strcmp(candidate_target, name) != 0) continue;
        double slot = source_slot(game, candidate);
        if (!have || slot < best_slot) { have = true; best_slot = slot; next = candidate; }
      }
    }
    if (!next) break;
    bool seen = false;
    for (size_t i = 0; i < visited_count; i++) if (qa_same_actor(visited[i], next->actor.id)) seen = true;
    if (seen) { free(visited); return NULL; }
    if (visited_count == visited_cap) {
      visited_cap *= 2;
      qa_actor_id *grown = realloc(visited, visited_cap * sizeof *grown);
      if (!grown) { free(visited); qa_raise("Error", 0, 0, "Out of memory in hint findStart"); }
      visited = grown;
    }
    visited[visited_count++] = next->actor.id;
    last = current = next;
  }
  free(visited);
  return last && (qa_js_to_uint32(last->spawnflags) & HINT_ENDPOINT) != 0 ? last : NULL;
}
qa_q2_entity *qa_q2mpm_hints_other_end(qa_q2mpm_hints *hints, qa_q2_entity *entity, qa_q2_game_services *game) {
  return qa_q2mpm_hints_find_start(hints, entity, game);
}
static bool visible_from(qa_q2_game_services *game, qa_actor_id actor, qa_q2_entity *target) {
  qa_body_state_result body = qa_shared_body_table_read(game->host->bodies, actor);
  if (!body.has_value) return false;
  qa_q2_entity *actor_entity = game->entity(game->ctx, &actor);
  qa_vec3 start = body.value.origin;
  start.z += actor_entity ? actor_entity->view_height : 22;
  qa_vec3 destination = q2mpm_body(game, target).origin;
  qa_vec3 end = destination;
  end.z += target->view_height;
  return q2mpm_trace(game, start, end, NULL, &actor, QA_Q2M_MASK_OPAQUE).fraction == 1;
}
bool qa_q2mpm_hints_check(qa_q2mpm_hints *hints, qa_q2m_context *context) {
  qa_q2_game_services *game = context->game;
  qa_q2_entity *entity = context->entity;
  qa_q2m_state *state = context->state;
  if (!hints->present || !entity->has_enemy || state->stand_ground
      || strcmp(entity->classname, "monster_turret") == 0
      || (q2mpm_rr(game) && state->alt.has_pathing)) return false;
  qa_actor_id enemy_actor = entity->enemy;
  qa_body_state_result enemy = qa_shared_body_table_read(game->host->bodies, enemy_actor);
  if (!enemy.has_value) return false;
  qa_vec3 origin = q2mpm_body(game, entity).origin;
  size_t total = 0;
  for (size_t i = 0; i < hints->start_count; i++) {
    qa_q2_entity *scratch[1];
    total += chain_collect(hints, game, hints->starts[i], scratch, 0);
  }
  qa_q2_entity **all = malloc((total ? total : 1) * sizeof *all);
  if (!all) qa_raise("Error", 0, 0, "Out of memory in hint check");
  size_t all_count = 0;
  for (size_t i = 0; i < hints->start_count; i++)
    all_count += chain_collect(hints, game, hints->starts[i], all + all_count, total - all_count);
  qa_q2_entity *start = NULL;
  qa_q2_entity *destination = NULL;
  /* Monster-side nodes near and visible; represented chains. */
  double represented[128];
  size_t represented_count = 0;
  for (size_t i = 0; i < all_count; i++) {
    qa_q2_entity *node = all[i];
    if (qa_hypot3(qa_sub3_double(origin, q2mpm_body(game, node).origin)) > 512) continue;
    if (!qa_q2m_visible(context, &node->actor.id)) continue;
    qa_q2mpm_hint_node *info = node_of(hints, node->actor.id);
    double chain = info ? info->chain : -2;
    bool have = false;
    for (size_t j = 0; j < represented_count; j++) if (represented[j] == chain) have = true;
    if (!have && represented_count < 128) represented[represented_count++] = chain;
  }
  double target_chains[128];
  size_t target_chain_count = 0;
  qa_q2_entity **target_nodes = malloc((all_count ? all_count : 1) * sizeof *target_nodes);
  if (!target_nodes) { free(all); qa_raise("Error", 0, 0, "Out of memory in hint check"); }
  size_t target_count = 0;
  for (size_t i = 0; i < all_count; i++) {
    qa_q2_entity *node = all[i];
    qa_q2mpm_hint_node *info = node_of(hints, node->actor.id);
    double chain = info ? info->chain : -2;
    bool represented_node = false;
    for (size_t j = 0; j < represented_count; j++) if (represented[j] == chain) represented_node = true;
    if (!represented_node) continue;
    if (qa_hypot3(qa_sub3_double(enemy.value.origin, q2mpm_body(game, node).origin)) > 512) continue;
    if (!visible_from(game, enemy_actor, node)) continue;
    target_nodes[target_count++] = node;
    bool have = false;
    for (size_t j = 0; j < target_chain_count; j++) if (target_chains[j] == chain) have = true;
    if (!have && target_chain_count < 128) target_chains[target_chain_count++] = chain;
  }
  if (target_count == 0) { free(target_nodes); free(all); return false; }
  for (size_t i = 0; i < all_count; i++) {
    qa_q2_entity *node = all[i];
    if (qa_hypot3(qa_sub3_double(origin, q2mpm_body(game, node).origin)) > 512) continue;
    if (!qa_q2m_visible(context, &node->actor.id)) continue;
    qa_q2mpm_hint_node *info = node_of(hints, node->actor.id);
    double chain = info ? info->chain : -2;
    bool wanted = false;
    for (size_t j = 0; j < target_chain_count; j++) if (target_chains[j] == chain) wanted = true;
    if (wanted) start = node;
  }
  if (!start) { free(target_nodes); free(all); return false; }
  qa_q2mpm_hint_node *start_info = node_of(hints, start->actor.id);
  double chain = start_info ? start_info->chain : -2;
  for (size_t i = 0; i < target_count; i++) {
    qa_q2mpm_hint_node *info = node_of(hints, target_nodes[i]->actor.id);
    double node_chain = info ? info->chain : -2;
    if (node_chain == chain && qa_hypot3(qa_sub3_double(origin, q2mpm_body(game, target_nodes[i]).origin)) < 10000000)
      destination = target_nodes[i];
  }
  free(target_nodes);
  free(all);
  if (!destination) return false;
  qa_q2mpm_hint_monster *monster = monster_of(hints, context);
  monster->has_goal = true;
  monster->goal = destination->actor.id;
  qa_q2mpm_hints_go(hints, context, start);
  return true;
}
bool qa_q2mpm_hints_check_lost(qa_q2mpm_hints *hints, qa_q2m_context *context) {
  double now = q2mpm_now(context->game);
  qa_q2mpm_hint_monster *source = monster_of(hints, context);
  if (context->state->trail_time + 5 > now || source->last_time + 10 > now) return false;
  source->last_time = now;
  return qa_q2mpm_hints_check(hints, context);
}
void qa_q2mpm_hints_go(qa_q2mpm_hints *hints, qa_q2m_context *context, qa_q2_entity *point) {
  (void)hints;
  qa_q2_game_services *game = context->game;
  qa_q2_entity *entity = context->entity;
  qa_q2m_state *state = context->state;
  qa_vec3 a = q2mpm_body(game, point).origin;
  qa_vec3 b = q2mpm_body(game, entity).origin;
  state->ideal_yaw = qa_vector_to_angles_with_rule(
      qa_sub3_double(a, b),
      QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  entity->has_goal = true;
  entity->goal = point->actor.id;
  state->has_move_target = true;
  state->move_target = point->actor.id;
  state->pause_time = 0;
  state->alt.hint_path = true;
  state->has_sound_target = false;
  state->pursuit_last_seen = false;
  state->pursue_next = false;
  state->pursue_temporary = false;
  state->search_time = q2mpm_now(game);
  context->run(context->impl_ctx, context);
}
void qa_q2mpm_hints_stop(qa_q2mpm_hints *hints, qa_q2m_context *context) {
  qa_q2_game_services *game = context->game;
  qa_q2_entity *entity = context->entity;
  qa_q2m_state *state = context->state;
  qa_q2mpm_hint_monster *source = monster_of(hints, context);
  entity->has_goal = false;
  state->has_move_target = false;
  source->last_time = q2mpm_now(game);
  source->has_goal = false;
  state->alt.hint_path = false;
  if (entity->has_enemy && q2mpm_live(game, entity->enemy) && qa_q2m_health(game, &entity->enemy) >= 1) {
    if (qa_q2m_visible_enemy(context)) qa_q2m_monsters_found_target(hints->monsters, context);
    else qa_q2m_monsters_hunt_target(hints->monsters, context);
    return;
  }
  entity->has_enemy = false;
  state->pause_time = q2mpm_rr(game) ? HOLD_FOREVER : q2mpm_now(game) + 100000000;
  context->stand(context->impl_ctx, context);
}
bool qa_q2mpm_hints_run(qa_q2mpm_hints *hints, qa_q2m_context *context, double distance) {
  if (!context->state->alt.hint_path) return false;
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  (void)context->move_to_goal(context->impl_ctx, context, distance);
  if (!q2mpm_live(game, entity->actor.id)) return true;
  if (!entity->has_enemy || !q2mpm_live(game, entity->enemy)) {
    entity->has_enemy = false;
    qa_q2mpm_hints_stop(hints, context);
    return true;
  }
  qa_q2_entity *target = game->entity(game->ctx, &entity->enemy);
  bool has_real = true;
  qa_actor_id real_enemy = entity->enemy;
  if (target && strcmp(target->classname, "player_noise") == 0) {
    has_real = target->has_owner;
    real_enemy = target->owner;
  }
  if (!has_real) {
    entity->has_enemy = false;
    qa_q2mpm_hints_stop(hints, context);
    return true;
  }
  if (qa_q2m_visible(context, &real_enemy)) qa_q2mpm_hints_stop(hints, context);
  else if (game->options.mode == QA_Q2_MODE_COOP) (void)context->find_target(context->impl_ctx, context);
  return true;
}
void qa_q2mpm_hints_touch(qa_q2mpm_hints *hints, qa_q2_entity *hint, qa_q2_game_services *game, qa_q2m_context *context) {
  if (!context->state->has_move_target || !qa_same_actor(context->state->move_target, hint->actor.id)) return;
  qa_q2mpm_hint_monster *source = monster_of(hints, context);
  if (source->has_goal && qa_same_actor(source->goal, hint->actor.id)) {
    qa_q2mpm_hints_stop(hints, context);
    return;
  }
  qa_q2mpm_hint_node *node = node_of(hints, hint->actor.id);
  qa_q2_entity *next = NULL;
  bool goal_found = false;
  if (node && node->chain >= 0 && (size_t)node->chain < hints->start_count) {
    qa_actor_id start = hints->starts[(size_t)node->chain];
    size_t capacity = chain_collect(hints, game, start, NULL, 0);
    qa_q2_entity **chain = malloc((capacity ? capacity : 1) * sizeof *chain);
    if (!chain) qa_raise("Error", 0, 0, "Out of memory in hint touch");
    size_t count = chain_collect(hints, game, start, chain, capacity);
    for (size_t i = 0; i < count; i++) {
      qa_q2_entity *entry = chain[i];
      qa_q2mpm_hint_node *entry_node = node_of(hints, entry->actor.id);
      bool has_following = entry_node && entry_node->has_next;
      qa_actor_id following = has_following ? entry_node->next : entry->actor.id;
      if (qa_same_actor(entry->actor.id, hint->actor.id)) {
        next = has_following ? game->entity(game->ctx, &following) : NULL;
        break;
      }
      if (source->has_goal && qa_same_actor(entry->actor.id, source->goal)) goal_found = true;
      if (has_following && qa_same_actor(following, hint->actor.id) && goal_found) {
        next = entry;
        break;
      }
    }
    free(chain);
  }
  if (!next) {
    qa_q2mpm_hints_stop(hints, context);
    return;
  }
  qa_q2mpm_hints_go(hints, context, next);
  if (hint->wait != 0 && context->entity->think.fn != NULL)
    game->schedule(game->ctx, context->entity, hint->wait, context->entity->think);
}
void qa_q2mpm_hints_checkpoint_free(qa_q2mpm_hints_checkpoint *checkpoint) {
  free(checkpoint->starts);
  free(checkpoint->nodes);
  free(checkpoint->monsters);
  memset(checkpoint, 0, sizeof *checkpoint);
}
bool qa_q2mpm_hints_capture(qa_q2mpm_hints *hints, qa_q2_game_services *game, qa_q2mpm_hints_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->version = 1;
  out->present = hints->present;
  if (hints->start_count) {
    out->starts = malloc(hints->start_count * sizeof *out->starts);
    if (!out->starts) return false;
    out->start_count = hints->start_count;
    for (size_t i = 0; i < hints->start_count; i++) {
      out->starts[i].slot = (double)hints->starts[i].slot;
      out->starts[i].generation = (double)hints->starts[i].generation;
    }
  }
  size_t live_nodes = 0;
  for (size_t i = 0; i < hints->node_count; i++)
    if (q2mpm_live(game, hints->nodes[i].actor)) live_nodes++;
  if (live_nodes) {
    out->nodes = calloc(live_nodes, sizeof *out->nodes);
    if (!out->nodes) { qa_q2mpm_hints_checkpoint_free(out); return false; }
  }
  for (size_t i = 0; i < hints->node_count; i++) {
    if (!q2mpm_live(game, hints->nodes[i].actor)) continue;
    qa_q2mpm_hint_node_checkpoint *entry = &out->nodes[out->node_count++];
    entry->actor.slot = (double)hints->nodes[i].actor.slot;
    entry->actor.generation = (double)hints->nodes[i].actor.generation;
    entry->chain = hints->nodes[i].chain;
    const qa_actor_id *next = hints->nodes[i].has_next ? &hints->nodes[i].next : NULL;
    entry->has_next = qa_q2_save_actor(next, &entry->next);
  }
  size_t live_monsters = 0;
  for (size_t i = 0; i < hints->pursuer_count; i++)
    if (q2mpm_live(game, hints->pursuers[i].actor)) live_monsters++;
  if (live_monsters) {
    out->monsters = calloc(live_monsters, sizeof *out->monsters);
    if (!out->monsters) { qa_q2mpm_hints_checkpoint_free(out); return false; }
  }
  for (size_t i = 0; i < hints->pursuer_count; i++) {
    if (!q2mpm_live(game, hints->pursuers[i].actor)) continue;
    qa_q2mpm_hint_monster_checkpoint *entry = &out->monsters[out->monster_count++];
    entry->actor.slot = (double)hints->pursuers[i].actor.slot;
    entry->actor.generation = (double)hints->pursuers[i].actor.generation;
    const qa_actor_id *goal = hints->pursuers[i].has_goal ? &hints->pursuers[i].goal : NULL;
    entry->has_goal = qa_q2_save_actor(goal, &entry->goal);
    entry->last_time = hints->pursuers[i].last_time;
  }
  return true;
}
static qa_actor_id restore_reference(qa_q2_game_services *game, qa_saved_actor_id saved) {
  qa_owned_actor resolved; bool has_resolved = qa_session_actor_registry_resolve_saved(game->host->actors, saved, &resolved);
  return has_resolved ? resolved.id : qa_session_actor_registry_reference_saved(game->host->actors, saved, NULL);
}
void qa_q2mpm_hints_restore(qa_q2mpm_hints *hints, qa_q2_game_services *game, const qa_q2mpm_hints_checkpoint *checkpoint) {
  hints->present = checkpoint->present;
  hints->start_count = 0;
  hints->node_count = 0;
  hints->pursuer_count = 0;
  for (size_t i = 0; i < checkpoint->start_count; i++)
    starts_push(hints, restore_reference(game, checkpoint->starts[i]));
  for (size_t i = 0; i < checkpoint->node_count; i++) {
    const qa_q2mpm_hint_node_checkpoint *node = &checkpoint->nodes[i];
    qa_actor_id actor = restore_reference(game, node->actor);
    qa_actor_id next = node->has_next ? restore_reference(game, node->next) : actor;
    (void)node_set(hints, actor, node->chain, node->has_next, next);
  }
  for (size_t i = 0; i < checkpoint->monster_count; i++) {
    const qa_q2mpm_hint_monster_checkpoint *saved = &checkpoint->monsters[i];
    if (hints->pursuer_count == hints->pursuer_cap) {
      size_t cap = hints->pursuer_cap ? hints->pursuer_cap * 2 : 8;
      qa_q2mpm_hint_monster *grown = realloc(hints->pursuers, cap * sizeof *grown);
      if (!grown) qa_raise("Error", 0, 0, "Out of memory restoring hint pursuers");
      hints->pursuers = grown;
      hints->pursuer_cap = cap;
    }
    qa_q2mpm_hint_monster *monster = &hints->pursuers[hints->pursuer_count++];
    memset(monster, 0, sizeof *monster);
    monster->actor = restore_reference(game, saved->actor);
    monster->has_goal = saved->has_goal;
    if (saved->has_goal) monster->goal = restore_reference(game, saved->goal);
    monster->last_time = saved->last_time;
  }
}
bool qa_q2mpm_decode_hints_checkpoint(const qa_save_reader *reader, qa_q2mpm_hints_checkpoint *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader field;
  if (!qa_save_reader_field(reader, "version", &field)) return false;
  out->version = qa_save_reader_literal_number(&field, 1);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "present", &field)) return false;
  out->present = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "starts", &field)) return false;
  out->start_count = qa_save_reader_list_count(&field);
  if (out->start_count) {
    out->starts = malloc(out->start_count * sizeof *out->starts);
    if (!out->starts) { qa_save_reader_destroy(&field); return false; }
    for (size_t i = 0; i < out->start_count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&field, i, &item)) { qa_save_reader_destroy(&field); qa_q2mpm_hints_checkpoint_free(out); return false; }
      out->starts[i] = qa_read_saved_actor(&item);
      qa_save_reader_destroy(&item);
    }
  }
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "nodes", &field)) { qa_q2mpm_hints_checkpoint_free(out); return false; }
  out->node_count = qa_save_reader_list_count(&field);
  if (out->node_count) {
    out->nodes = calloc(out->node_count, sizeof *out->nodes);
    if (!out->nodes) { qa_save_reader_destroy(&field); qa_q2mpm_hints_checkpoint_free(out); return false; }
    for (size_t i = 0; i < out->node_count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&field, i, &item)) { qa_save_reader_destroy(&field); qa_q2mpm_hints_checkpoint_free(out); return false; }
      qa_save_reader sub;
      if (!qa_save_reader_field(&item, "actor", &sub)) { qa_save_reader_destroy(&item); qa_save_reader_destroy(&field); qa_q2mpm_hints_checkpoint_free(out); return false; }
      out->nodes[i].actor = qa_read_saved_actor(&sub);
      qa_save_reader_destroy(&sub);
      if (!qa_save_reader_field(&item, "chain", &sub)) { qa_save_reader_destroy(&item); qa_save_reader_destroy(&field); qa_q2mpm_hints_checkpoint_free(out); return false; }
      out->nodes[i].chain = qa_save_reader_integer_min(&sub, -1);
      qa_save_reader_destroy(&sub);
      if (!qa_save_reader_field(&item, "next", &sub)) { qa_save_reader_destroy(&item); qa_save_reader_destroy(&field); qa_q2mpm_hints_checkpoint_free(out); return false; }
      out->nodes[i].has_next = !qa_save_reader_is_null(&sub);
      if (out->nodes[i].has_next) out->nodes[i].next = qa_read_saved_actor(&sub);
      qa_save_reader_destroy(&sub);
      qa_save_reader_destroy(&item);
    }
  }
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "monsters", &field)) { qa_q2mpm_hints_checkpoint_free(out); return false; }
  out->monster_count = qa_save_reader_list_count(&field);
  if (out->monster_count) {
    out->monsters = calloc(out->monster_count, sizeof *out->monsters);
    if (!out->monsters) { qa_save_reader_destroy(&field); qa_q2mpm_hints_checkpoint_free(out); return false; }
    for (size_t i = 0; i < out->monster_count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&field, i, &item)) { qa_save_reader_destroy(&field); qa_q2mpm_hints_checkpoint_free(out); return false; }
      qa_save_reader sub;
      if (!qa_save_reader_field(&item, "actor", &sub)) { qa_save_reader_destroy(&item); qa_save_reader_destroy(&field); qa_q2mpm_hints_checkpoint_free(out); return false; }
      out->monsters[i].actor = qa_read_saved_actor(&sub);
      qa_save_reader_destroy(&sub);
      if (!qa_save_reader_field(&item, "goal", &sub)) { qa_save_reader_destroy(&item); qa_save_reader_destroy(&field); qa_q2mpm_hints_checkpoint_free(out); return false; }
      out->monsters[i].has_goal = !qa_save_reader_is_null(&sub);
      if (out->monsters[i].has_goal) out->monsters[i].goal = qa_read_saved_actor(&sub);
      qa_save_reader_destroy(&sub);
      if (!qa_save_reader_field(&item, "lastTime", &sub)) { qa_save_reader_destroy(&item); qa_save_reader_destroy(&field); qa_q2mpm_hints_checkpoint_free(out); return false; }
      out->monsters[i].last_time = qa_save_reader_finite(&sub);
      qa_save_reader_destroy(&sub);
      qa_save_reader_destroy(&item);
    }
  }
  qa_save_reader_destroy(&field);
  bool invalid = out->start_count > MAX_HINT_CHAINS;
  for (size_t i = 0; i < out->node_count; i++)
    if (out->nodes[i].chain >= (double)out->start_count) invalid = true;
  if (invalid) { qa_q2mpm_hints_checkpoint_free(out); qa_save_reader_fail(reader, "%s", "invalid hint chain index"); }
  return true;
}
