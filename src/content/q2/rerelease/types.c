/* Port of src/content/q2/rerelease/types.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/types.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

noreturn void qa_q2rr_error(const char *format, ...) {
  va_list args; va_start(args, format);
  fprintf(stderr, "Error: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  abort();
}

char *qa_q2rr_dup(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *out = malloc(n);
  if (out) memcpy(out, s, n);
  return out;
}

bool qa_q2rr_ieq(const char *a, const char *b) {
  if (!a || !b) return a == b;
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    a++; b++;
  }
  return *a == *b;
}
bool qa_q2rr_contains(const char *s, const char *needle) { return s && needle && strstr(s, needle) != NULL; }
bool qa_q2rr_starts_with(const char *s, const char *prefix) {
  return s && prefix && strncmp(s, prefix, strlen(prefix)) == 0;
}

qa_q2_fog_state qa_q2_create_fog(void) { qa_q2_fog_state s = {0}; return s; }

const char *qa_q2_coop_respawn_state_name(qa_q2_coop_respawn_state s) {
  switch (s) {
    case QA_Q2_COOP_IN_COMBAT: return "in-combat";
    case QA_Q2_COOP_BAD_AREA: return "bad-area";
    case QA_Q2_COOP_BLOCKED: return "blocked";
    case QA_Q2_COOP_WAITING: return "waiting";
    case QA_Q2_COOP_NO_LIVES: return "no-lives";
    default: return "none";
  }
}
const char *qa_q2rr_hand_name(qa_q2rr_hand h) {
  return h == QA_Q2RR_HAND_LEFT ? "left" : h == QA_Q2RR_HAND_CENTER ? "center" : "right";
}

static void copy_str(char *dst, size_t cap, const char *src) {
  if (!src) src = "";
  size_t n = strlen(src);
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n); dst[n] = '\0';
}

void qa_q2rr_player_extra_state_init(qa_q2rr_player_extra_state *s, double seat, const char *social_id) {
  memset(s, 0, sizeof *s);
  s->seat = seat;
  copy_str(s->social_id, sizeof s->social_id, social_id);
  s->auto_shield = -1;
  copy_str(s->help_image, sizeof s->help_image, "friend");
  s->fog = qa_q2_create_fog();
  s->wanted_fog = qa_q2_create_fog();
}
void qa_q2rr_player_extra_state_destroy(qa_q2rr_player_extra_state *s) {
  free(s->help_points); s->help_points = NULL; s->help_point_count = s->help_point_cap = 0;
}
bool qa_q2rr_player_extra_state_clone(qa_q2rr_player_extra_state *dst, const qa_q2rr_player_extra_state *src) {
  qa_vec3 *pts = NULL;
  if (src->help_point_count) {
    pts = malloc(src->help_point_count * sizeof *pts);
    if (!pts) return false;
    memcpy(pts, src->help_points, src->help_point_count * sizeof *pts);
  }
  *dst = *src;
  dst->help_points = pts;
  dst->help_point_cap = src->help_point_count;
  return true;
}

qa_q2_rerelease_options qa_q2_create_rerelease_options(const qa_q2_rerelease_options_changes *c) {
  qa_q2_rerelease_options o = {0};
  o.coop_squad_respawn = true; o.coop_instanced_items = true; o.coop_lives = false; o.coop_num_lives = 2;
  o.deathmatch_spawn_farthest = true; o.auto_save_minimum_time = 60;
  if (c) {
    if (c->has_coop_squad_respawn) o.coop_squad_respawn = c->v.coop_squad_respawn;
    if (c->has_coop_instanced_items) o.coop_instanced_items = c->v.coop_instanced_items;
    if (c->has_coop_lives) o.coop_lives = c->v.coop_lives;
    if (c->has_coop_num_lives) o.coop_num_lives = c->v.coop_num_lives;
    if (c->has_deathmatch_force_respawn) o.deathmatch_force_respawn = c->v.deathmatch_force_respawn;
    if (c->has_deathmatch_no_fall_damage) o.deathmatch_no_fall_damage = c->v.deathmatch_no_fall_damage;
    if (c->has_deathmatch_spawn_farthest) o.deathmatch_spawn_farthest = c->v.deathmatch_spawn_farthest;
    if (c->has_deathmatch_force_respawn_time) o.deathmatch_force_respawn_time = c->v.deathmatch_force_respawn_time;
    if (c->has_deathmatch_allow_exit) o.deathmatch_allow_exit = c->v.deathmatch_allow_exit;
    if (c->has_coop_player_collision) o.coop_player_collision = c->v.coop_player_collision;
    if (c->has_auto_save_minimum_time) o.auto_save_minimum_time = c->v.auto_save_minimum_time;
  }
  return o;
}

bool qa_q2_is_n64(const qa_q2_game_options *o) { return o && o->map_name && strncmp(o->map_name, "q64/", 4) == 0; }
bool qa_q2_uses_instanced_items(qa_q2_rerelease_options o) { return o.coop_instanced_items || o.coop_squad_respawn; }
