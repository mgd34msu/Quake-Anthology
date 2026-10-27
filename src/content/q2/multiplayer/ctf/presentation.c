/* Port of src/content/q2/multiplayer/ctf/presentation.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/ctf/presentation.h"
#include "../multiplayer_internal.h"
#include <ctype.h>

struct tech_name { const char *classname, *name; };
static const struct tech_name TECH_NAMES[4] = {
    {"item_tech1", "Disruptor Shield"}, {"item_tech2", "Power Amplifier"},
    {"item_tech3", "Time Accel"}, {"item_tech4", "AutoDoc"},
};
struct location_row { const char *classname; double priority; };
static const struct location_row LOCATIONS[] = {
    {"item_flag_team1", 1}, {"item_flag_team2", 1}, {"item_quad", 2}, {"item_invulnerability", 2},
    {"weapon_bfg", 3},
    {"weapon_railgun", 4}, {"weapon_rocketlauncher", 4}, {"weapon_hyperblaster", 4}, {"weapon_chaingun", 4},
    {"weapon_grenadelauncher", 4}, {"weapon_machinegun", 4}, {"weapon_supershotgun", 4}, {"weapon_shotgun", 4},
    {"item_power_screen", 5}, {"item_power_shield", 5},
    {"item_armor_body", 6}, {"item_armor_combat", 6}, {"item_armor_jacket", 6},
    {"item_silencer", 7}, {"item_breather", 7}, {"item_enviro", 7}, {"item_adrenaline", 7},
    {"item_bandolier", 8}, {"item_pack", 8},
};
#define LOCATION_COUNT (sizeof LOCATIONS / sizeof *LOCATIONS)

struct qa_q2_ctf_presentation {
  qa_q2_ctf_context *context;
  qa_q2_ctf_flags *flags;
};

qa_q2_ctf_presentation *qa_q2_ctf_presentation_create(qa_q2_ctf_context *context, qa_q2_ctf_flags *flags) {
  qa_q2_ctf_presentation *p = calloc(1, sizeof *p);
  if (p == NULL) return NULL;
  p->context = context;
  p->flags = flags;
  return p;
}
void qa_q2_ctf_presentation_destroy(qa_q2_ctf_presentation *p) { free(p); }

const char *qa_ctf_tech(qa_q2_game_services *g, qa_actor_id actor) {
  for (int i = 0; i < 4; i++) {
    char item[32];
    snprintf(item, sizeof item, "q2:%s", TECH_NAMES[i].classname);
    if (qa_q2mp_inv_count(g, actor, item) != 0) return TECH_NAMES[i].classname;
  }
  return NULL;
}

/* Small growable string. */
struct sb { char *data; size_t len, cap; };
static void sb_add(struct sb *b, const char *text) {
  size_t n = strlen(text);
  if (b->len + n + 1 > b->cap) {
    b->cap = (b->len + n + 1) * 2;
    b->data = qa_q2mp_xalloc(realloc(b->data, b->cap));
  }
  memcpy(b->data + b->len, text, n + 1);
  b->len += n;
}
static void sb_addf(struct sb *b, const char *format, ...) {
  char tmp[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(tmp, sizeof tmp, format, args);
  va_end(args);
  sb_add(b, tmp);
}
static void pad_start(char *out, size_t size, const char *text, size_t width) {
  size_t len = strlen(text);
  size_t pad = len < width ? width - len : 0;
  if (size == 0) return;
  size_t total = pad + len;
  if (total >= size) total = size - 1;
  size_t i = 0;
  for (; i < pad && i < total; i++) out[i] = ' ';
  for (size_t j = 0; i < total && text[j]; j++, i++) out[i] = text[j];
  out[i] = '\0';
}
static void pad_end(char *out, size_t size, const char *text, size_t width) {
  snprintf(out, size, "%s", text);
  size_t len = strlen(out);
  while (len < width && len + 1 < size) {
    out[len++] = ' ';
    out[len] = '\0';
  }
}

struct rows { qa_q2_ctf_score_row *rows; size_t count; };
static int row_cmp(const void *a, const void *b) {
  const qa_q2_ctf_score_row *ra = a, *rb = b;
  if (rb->score != ra->score) return rb->score > ra->score ? 1 : -1;
  if (ra->slot != rb->slot) return ra->slot < rb->slot ? -1 : 1;
  return 0;
}
static struct rows team_rows(qa_q2_ctf_presentation *p, qa_q2_game_services *g, double team) {
  size_t pn = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
  struct rows out = {0};
  out.rows = qa_q2mp_xalloc(malloc((pn == 0 ? 1 : pn) * sizeof *out.rows));
  for (size_t i = 0; i < pn; i++) {
    qa_q2_ctf_player_state *state = qa_q2_ctf_context_get(p->context, players[i]);
    qa_q2mp_player_state *player = p->context->hooks.player(p->context->hooks.ctx, players[i]);
    if (state == NULL || state->team != team || player == NULL) continue;
    qa_q2_ctf_score_row *row = &out.rows[out.count++];
    *row = (qa_q2_ctf_score_row){0};
    row->slot = player->slot;
    row->name = player->name;
    row->score = player->score;
    row->ping = player->ping < 999 ? player->ping : 999;
    row->has_carried_flag = qa_ctf_carried_flag(g, players[i], &row->carried_flag);
  }
  qsort(out.rows, out.count, sizeof *out.rows, row_cmp);
  return out;
}

void qa_q2_ctf_presentation_scoreboard(qa_q2_ctf_presentation *p, qa_q2_entity *entity, qa_q2_game_services *g) {
  struct rows red = team_rows(p, g, 1), blue = team_rows(p, g, 2), spectators = team_rows(p, g, 0);
  double totals[2] = {0, 0};
  for (size_t i = 0; i < red.count; i++) totals[0] += red.rows[i].score;
  for (size_t i = 0; i < blue.count; i++) totals[1] += blue.rows[i].score;
  char total0[32], total1[32], count0[32], count1[32], padded0[40], padded1[40], ended0[40], ended1[40];
  qa_q2mp_format_number(total0, sizeof total0, totals[0]);
  qa_q2mp_format_number(total1, sizeof total1, totals[1]);
  qa_q2mp_format_number(count0, sizeof count0, (double)red.count);
  qa_q2mp_format_number(count1, sizeof count1, (double)blue.count);
  pad_start(padded0, sizeof padded0, total0, 4);
  pad_start(padded1, sizeof padded1, total1, 4);
  pad_end(ended0, sizeof ended0, count0, 3);
  pad_end(ended1, sizeof ended1, count1, 3);
  struct sb layout = {0};
  sb_addf(&layout, "if 24 xv 8 yv 8 pic 24 endif xv 40 yv 28 string \"%s/%s\" xv 98 yv 12 num 2 18 if 25 xv 168 yv 8 pic 25 endif xv 200 yv 28 string \"%s/%s\" xv 256 yv 12 num 2 20 ",
          padded0, ended0, padded1, ended1);
  size_t red_shown = 0, blue_shown = 0;
  for (int index = 0; index < 16; index++) {
    struct { struct rows *rows; int x; int enemy; } teams[2] = {{&red, 0, 2}, {&blue, 160, 1}};
    for (int ti = 0; ti < 2; ti++) {
      if ((size_t)index >= teams[ti].rows->count) continue;
      const qa_q2_ctf_score_row *row = &teams[ti].rows->rows[index];
      int y = 42 + index * 8;
      char slot[32], score[32], ping[32];
      qa_q2mp_format_number(slot, sizeof slot, row->slot);
      qa_q2mp_format_number(score, sizeof score, row->score);
      qa_q2mp_format_number(ping, sizeof ping, row->ping);
      char text[256];
      if (row->has_carried_flag && row->carried_flag == teams[ti].enemy)
        snprintf(text, sizeof text, "ctf %d %d %s %s %s xv %d yv %d picn sbfctf%d ", teams[ti].x, y, slot, score, ping, teams[ti].x + 56, y, teams[ti].enemy);
      else
        snprintf(text, sizeof text, "ctf %d %d %s %s %s ", teams[ti].x, y, slot, score, ping);
      if (layout.len + strlen(text) < 1000) {
        sb_add(&layout, text);
        if (teams[ti].x == 0) red_shown++; else blue_shown++;
      }
    }
  }
  size_t max_shown = red_shown > blue_shown ? red_shown : blue_shown;
  long y = (long)(max_shown + 1) * 8 + 42;
  if (spectators.count != 0) {
    char header[128];
    snprintf(header, sizeof header, "xv 0 yv %ld string2 \"Spectators\" ", y);
    if (layout.len + strlen(header) < 1000) {
      sb_add(&layout, header);
      y += 8;
      for (size_t i = 0; i < spectators.count; i++) {
        const qa_q2_ctf_score_row *row = &spectators.rows[i];
        char slot[32], score[32], ping[32];
        qa_q2mp_format_number(slot, sizeof slot, row->slot);
        qa_q2mp_format_number(score, sizeof score, row->score);
        qa_q2mp_format_number(ping, sizeof ping, row->ping);
        char text[256];
        snprintf(text, sizeof text, "ctf %d %ld %s %s %s ", (int)(i % 2) * 160, y + (long)(i / 2) * 8, slot, score, ping);
        if (layout.len + strlen(text) < 1000) sb_add(&layout, text);
      }
    }
  }
  struct { size_t total, shown; int x; } more[2] = {{red.count, red_shown, 8}, {blue.count, blue_shown, 168}};
  for (int i = 0; i < 2; i++)
    if (more[i].total > more[i].shown) {
      char text[256];
      snprintf(text, sizeof text, "xv %d yv %d string \"..and %zu more\" ", more[i].x, 42 + (int)more[i].shown * 8, more[i].total - more[i].shown);
      if (layout.len + strlen(text) < 1000) sb_add(&layout, text);
    }
  qa_q2_ctf_event event = {0};
  event.kind = "scoreboard";
  event.actor = entity->actor.id;
  event.red = red.rows; event.red_count = red.count;
  event.blue = blue.rows; event.blue_count = blue.count;
  event.spectators = spectators.rows; event.spectator_count = spectators.count;
  event.totals[0] = totals[0]; event.totals[1] = totals[1];
  event.captures[0] = p->context->match.team1;
  event.captures[1] = p->context->match.team2;
  event.layout = layout.data == NULL ? "" : layout.data;
  p->context->hooks.emit(p->context->hooks.ctx, event);
  free(layout.data);
  free(red.rows);
  free(blue.rows);
  free(spectators.rows);
}

bool qa_q2_ctf_presentation_identify(qa_q2_ctf_presentation *p, qa_q2_entity *entity, qa_q2_game_services *g, qa_actor_id *out) {
  (void)p;
  qa_vec3 origin = qa_q2_game_body(g, entity).origin;
  qa_q2_player_view_result view = g->host->player_view_state(g->host->ctx, entity->actor.id);
  qa_vec3 forward = qa_math_angle_vectors_rule(view.has_value ? view.value.view_angles : qa_q2_game_body(g, entity).angles, QA_ANGLE_RULE_NATIVE).forward;
  qa_trace_result trace = qa_q2mp_trace(g, origin, qa_q2_add(origin, qa_q2_scale(forward, 1024)), NULL, &entity->actor.id, 3);
  if (trace.hit.kind == QA_TRACE_HIT_ACTOR && g->host->is_player(g->host->ctx, trace.hit.actor)) {
    *out = trace.hit.actor;
    return true;
  }
  bool found = false;
  double alignment = 0.9;
  size_t pn = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
  for (size_t i = 0; i < pn; i++) {
    qa_q2_entity *other = qa_q2_game_entity_of(g, &players[i]);
    if (other == NULL || other == entity || strcmp(other->solid, "none") == 0) continue;
    double candidate = qa_q2_dot(forward, qa_q2_normalize(qa_q2_subtract(qa_q2_game_body(g, other).origin, origin)));
    if (candidate > alignment && qa_ctf_can_see(other, entity, g)) {
      *out = players[i];
      found = true;
      alignment = candidate;
    }
  }
  return found;
}

void qa_q2_ctf_presentation_hud(qa_q2_ctf_presentation *p, qa_q2_entity *entity, qa_q2_game_services *g, const char *status) {
  qa_q2_ctf_player_state *state = qa_ctf_player(p->context, entity->actor.id);
  qa_q2_ctf_match_state *match = &p->context->match;
  double blink = (double)((int64_t)trunc(qa_q2mp_now(g) * 10) & 8);
  qa_q2_ctf_event event = {0};
  event.kind = "hud";
  event.actor = entity->actor.id;
  event.team = state->team;
  event.captures[0] = match->team1;
  event.captures[1] = match->team2;
  event.flag_states[0] = qa_q2_ctf_flags_state(p->flags, g, 1);
  event.flag_states[1] = qa_q2_ctf_flags_state(p->flags, g, 2);
  event.has_carried_flag = qa_ctf_carried_flag(g, entity->actor.id, &event.carried_flag);
  event.tech = qa_ctf_tech(g, entity->actor.id);
  event.has_id_target = state->id_view && qa_q2_ctf_presentation_identify(p, entity, g, &event.id_target);
  event.has_blink_team = match->has_last_flag_capture && qa_q2mp_now(g) - match->last_flag_capture < 5 && blink != 0 && match->has_last_capture_team;
  if (event.has_blink_team) event.blink_team = match->last_capture_team;
  event.match = status;
  p->context->hooks.emit(p->context->hooks.ctx, event);
}

struct candidate { qa_q2_entity *target; double priority, distance; bool visible; };
static int candidate_cmp(const void *a, const void *b) {
  const struct candidate *ca = a, *cb = b;
  if (ca->visible != cb->visible) return cb->visible ? 1 : -1;
  if (ca->visible && ca->priority != cb->priority) return ca->priority < cb->priority ? -1 : 1;
  if (ca->distance != cb->distance) return ca->distance < cb->distance ? -1 : 1;
  return 0;
}

char *qa_q2_ctf_presentation_location(qa_q2_ctf_presentation *p, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_vec3 origin = qa_q2_game_body(g, entity).origin;
  size_t n = 0;
  qa_q2_entity *const *all = g->entities(g->ctx, &n);
  struct candidate *candidates = qa_q2mp_xalloc(malloc((n == 0 ? 1 : n) * sizeof *candidates));
  size_t count = 0;
  for (size_t i = 0; i < n; i++) {
    const struct location_row *entry = NULL;
    for (size_t j = 0; j < LOCATION_COUNT; j++)
      if (strcmp(LOCATIONS[j].classname, all[i]->classname) == 0) { entry = &LOCATIONS[j]; break; }
    if (entry == NULL) continue;
    double distance = qa_q2_length(qa_q2_subtract(qa_q2_game_body(g, all[i]).origin, origin));
    if (distance > 1024) continue;
    candidates[count++] = (struct candidate){all[i], entry->priority, distance, qa_ctf_can_see(all[i], entity, g)};
  }
  qsort(candidates, count, sizeof *candidates, candidate_cmp);
  if (count == 0) {
    free(candidates);
    return qa_q2mp_dup("nowhere");
  }
  qa_q2_entity *hot = candidates[0].target;
  free(candidates);
  const char *team = "";
  bool dup = false;
  for (size_t i = 0; i < n; i++)
    if (all[i] != hot && strcmp(all[i]->classname, hot->classname) == 0) dup = true;
  if (dup) {
    qa_q2_entity *red = qa_q2_ctf_flags_base(p->flags, g, 1);
    qa_q2_entity *blue = qa_q2_ctf_flags_base(p->flags, g, 2);
    if (red != NULL && blue != NULL) {
      double one = qa_q2_length(qa_q2_subtract(qa_q2_game_body(g, hot).origin, qa_q2_game_body(g, red).origin));
      double two = qa_q2_length(qa_q2_subtract(qa_q2_game_body(g, hot).origin, qa_q2_game_body(g, blue).origin));
      team = one < two ? "red " : one > two ? "blue " : "";
    }
  }
  qa_vec3 delta = qa_q2_subtract(origin, qa_q2_game_body(g, hot).origin);
  const char *where = fabs(delta.z) > fabs(delta.x) && fabs(delta.z) > fabs(delta.y) ? (delta.z > 0 ? "above" : "below") : "near";
  qa_vec3 point = qa_q2_add(origin, (qa_vec3){0, 0, qa_q2_game_body(g, entity).bounds.min.z + 1});
  const char *water = ((int64_t)g->host->point_contents(g->host->ctx, point) & 56) != 0 ? "in the water " : "";
  const char *name = qa_q2_item_module_item_name(p->context->hooks.items, hot->classname);
  char buffer[512];
  snprintf(buffer, sizeof buffer, "%s%s the %s%s", water, where, team, name == NULL ? hot->classname : name);
  return qa_q2mp_dup(buffer);
}

static char *armor_text(qa_q2_ctf_presentation *p, qa_q2_entity *entity, qa_q2_game_services *g, const qa_combat_state_result *combat) {
  if (!combat->has_value) return qa_q2mp_dup("no armor");
  qa_armor_state value = combat->value.armor;
  double cells = qa_q2mp_inv_count(g, entity->actor.id, "q2:ammo_cells");
  char power[128] = "";
  if (value.powered.kind != NULL && strcmp(value.powered.kind, "none") != 0 && cells > 0) {
    char cells_text[32];
    qa_q2mp_format_number(cells_text, sizeof cells_text, cells);
    snprintf(power, sizeof power, "Power %s with %s cells", strcmp(value.powered.kind, "screen") == 0 ? "Screen" : "Shield", cells_text);
  }
  char conventional[256] = "";
  qa_regular_armor_state regular = value.regular;
  if (regular.kind != QA_REGULAR_ARMOR_NONE && regular.points > 0) {
    char points[32];
    qa_q2mp_format_number(points, sizeof points, regular.points);
    if (regular.kind == QA_REGULAR_ARMOR_Q2) {
      qa_q2_inventory_item found;
      const char *name = regular.item != NULL && qa_q2_item_module_lookup(p->context->hooks.items, regular.item, &found) ? found.name : "armor";
      snprintf(conventional, sizeof conventional, "%s units of %s", points, name);
    } else {
      snprintf(conventional, sizeof conventional, "%s armor", points);
    }
  }
  char buffer[512];
  if (power[0] && conventional[0]) snprintf(buffer, sizeof buffer, "%s and %s", power, conventional);
  else if (power[0]) snprintf(buffer, sizeof buffer, "%s", power);
  else if (conventional[0]) snprintf(buffer, sizeof buffer, "%s", conventional);
  else snprintf(buffer, sizeof buffer, "no armor");
  return qa_q2mp_dup(buffer);
}

static char *sight_text(qa_q2_ctf_presentation *p, qa_q2_entity *entity, qa_q2_game_services *g) {
  size_t pn = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
  struct sb joined = {0};
  size_t count = 0;
  const char *first = NULL;
  /* Collect names then join "a, b and c". */
  const char **names = qa_q2mp_xalloc(malloc((pn == 0 ? 1 : pn) * sizeof *names));
  for (size_t i = 0; i < pn; i++) {
    if (qa_same_actor(players[i], entity->actor.id)) continue;
    qa_q2_entity *target = qa_q2_game_entity_of(g, &players[i]);
    if (target != NULL && qa_ctf_can_see(target, entity, g)) names[count++] = qa_ctf_name(p->context, players[i]);
  }
  char *out;
  if (count < 2) {
    out = qa_q2mp_dup(count == 0 ? "no one" : names[0]);
  } else {
    for (size_t i = 0; i + 1 < count; i++) {
      if (i > 0) sb_add(&joined, ", ");
      sb_add(&joined, names[i]);
    }
    sb_add(&joined, " and ");
    sb_add(&joined, names[count - 1]);
    out = joined.data;
  }
  (void)first;
  free(names);
  return out;
}

void qa_q2_ctf_presentation_say_team(qa_q2_ctf_presentation *p, qa_q2_entity *entity, qa_q2_game_services *g, const char *words) {
  if (!p->context->hooks.chat_allowed(p->context->hooks.ctx, entity->actor.id, g)) return;
  qa_q2_ctf_player_state *state = qa_ctf_player(p->context, entity->actor.id);
  qa_combat_state_result combat = qa_q2mp_combat_read(g, entity->actor.id);
  double health = combat.has_value ? combat.value.health : 0;
  char *text_source = qa_q2mp_dup(words);
  if (text_source[0] == '"') {
    size_t len = strlen(text_source);
    if (len >= 2 && text_source[len - 1] == '"') text_source[len - 1] = '\0';
    memmove(text_source, text_source + 1, strlen(text_source));
  }
  struct sb text = {0};
  for (size_t i = 0; text_source[i]; i++) {
    if (text_source[i] != '%' || text_source[i + 1] == '\0') {
      char ch[2] = {text_source[i], '\0'};
      sb_add(&text, ch);
      continue;
    }
    char character = text_source[++i];
    char *replacement = NULL;
    char buffer[256];
    switch (tolower((unsigned char)character)) {
      case 'l': replacement = qa_q2_ctf_presentation_location(p, entity, g); break;
      case 'a': replacement = armor_text(p, entity, g, &combat); break;
      case 'h': {
        if (health <= 0) snprintf(buffer, sizeof buffer, "dead");
        else {
          char health_text[32];
          qa_q2mp_format_number(health_text, sizeof health_text, health);
          snprintf(buffer, sizeof buffer, "%s health", health_text);
        }
        replacement = qa_q2mp_dup(buffer);
        break;
      }
      case 't': {
        const char *tech = qa_ctf_tech(g, entity->actor.id);
        const char *name = NULL;
        if (tech != NULL)
          for (int j = 0; j < 4; j++)
            if (strcmp(TECH_NAMES[j].classname, tech) == 0) name = TECH_NAMES[j].name;
        if (name == NULL) replacement = qa_q2mp_dup("no powerup");
        else {
          snprintf(buffer, sizeof buffer, "the %s", name);
          replacement = qa_q2mp_dup(buffer);
        }
        break;
      }
      case 'w': {
        qa_q2mp_weapon_state *weapon = p->context->hooks.weapons->get_state(p->context->hooks.weapons->ctx, entity->actor.id);
        if (weapon == NULL || weapon->weapon == NULL) {
          replacement = qa_q2mp_dup("none");
        } else {
          const qa_q2f_weapon_definition *def = p->context->hooks.weapons->definition(p->context->hooks.weapons->ctx, weapon->weapon);
          qa_q2_inventory_item found;
          const char *name = def != NULL && def->item != NULL && qa_q2_item_module_lookup(p->context->hooks.items, def->item, &found) ? found.name : weapon->weapon;
          replacement = qa_q2mp_dup(name);
        }
        break;
      }
      case 'n': replacement = sight_text(p, entity, g); break;
      default: {
        char ch[2] = {character, '\0'};
        replacement = qa_q2mp_dup(ch);
        break;
      }
    }
    sb_add(&text, replacement);
    free(replacement);
  }
  free(text_source);
  if (text.len > 1023) {
    text.data[1023] = '\0';
    text.len = 1023;
  }
  size_t pn = 0;
  const qa_actor_id *players = g->host->players(g->host->ctx, &pn);
  for (size_t i = 0; i < pn; i++) {
    qa_q2_ctf_player_state *member = qa_q2_ctf_context_get(p->context, players[i]);
    if (member != NULL && member->team == state->team)
      qa_ctf_print(g, &players[i], "chat", "(%s): %s\n", qa_ctf_name(p->context, entity->actor.id), text.data == NULL ? "" : text.data);
  }
  free(text.data);
}

void qa_q2_ctf_presentation_join_menu(qa_q2_ctf_presentation *p, qa_q2_entity *entity) {
  qa_q2_ctf_match_state *match = &p->context->match;
  bool locked = p->context->rules.match_lock && (strcmp(match->phase, "pregame") == 0 || strcmp(match->phase, "game") == 0);
  double counts[2] = {0, 0};
  for (size_t i = 0; i < p->context->state_count; i++) {
    if (p->context->states[i].state.team == 1) counts[0]++;
    else if (p->context->states[i].state.team == 2) counts[1]++;
  }
  char red[64], blue[64];
  char c0[32], c1[32];
  qa_q2mp_format_number(c0, sizeof c0, counts[0]);
  qa_q2mp_format_number(c1, sizeof c1, counts[1]);
  snprintf(red, sizeof red, "Join Red Team (%s)", c0);
  snprintf(blue, sizeof blue, "Join Blue Team (%s)", c1);
  char *labels[6] = {qa_q2mp_dup(red), qa_q2mp_dup(blue), qa_q2mp_dup("Chase Camera"), qa_q2mp_dup("Credits"), qa_q2mp_dup("Request match"), qa_q2mp_dup("Close")};
  qa_q2_ctf_menu_entry entries[6] = {
      {labels[0], locked || strcmp(p->context->rules.force_join, "blue") == 0 ? NULL : "join-red"},
      {labels[1], locked || strcmp(p->context->rules.force_join, "red") == 0 ? NULL : "join-blue"},
      {labels[2], "chase"},
      {labels[3], "credits"},
      {labels[4], p->context->rules.competition != 0 && strcmp(match->phase, "none") == 0 ? "match" : NULL},
      {labels[5], "close"},
  };
  qa_q2_ctf_event event = {0};
  event.kind = "menu";
  event.actor = entity->actor.id;
  event.title = "ThreeWave Capture the Flag";
  event.entries = entries;
  event.entry_count = 6;
  p->context->hooks.emit(p->context->hooks.ctx, event);
  for (int i = 0; i < 6; i++) free(labels[i]);
}

void qa_q2_ctf_presentation_stats(qa_q2_ctf_presentation *p, qa_q2_entity *entity, qa_q2_game_services *g) {
  struct sb text = {0};
  sb_add(&text, "Name             Frags Deaths Caps Base Carrier\n");
  for (size_t i = 0; i < p->context->match.ghost_count; i++) {
    qa_q2_ctf_ghost *ghost = &p->context->match.ghosts[i];
    char name[17];
    snprintf(name, sizeof name, "%s", ghost->name);
    char padded[24];
    pad_end(padded, sizeof padded, name, 16);
    char kills[32], deaths[32], captures[32], base[32], carrier[32];
    qa_q2mp_format_number(kills, sizeof kills, ghost->kills);
    qa_q2mp_format_number(deaths, sizeof deaths, ghost->deaths);
    qa_q2mp_format_number(captures, sizeof captures, ghost->captures);
    qa_q2mp_format_number(base, sizeof base, ghost->base_defense);
    qa_q2mp_format_number(carrier, sizeof carrier, ghost->carrier_defense);
    sb_addf(&text, "%s %s %s %s %s %s\n", padded, kills, deaths, captures, base, carrier);
  }
  if (text.len > 1399) {
    text.data[1399] = '\0';
    text.len = 1399;
  }
  qa_ctf_print(g, &entity->actor.id, NULL, "%s", text.data == NULL ? "" : text.data);
  free(text.data);
}
