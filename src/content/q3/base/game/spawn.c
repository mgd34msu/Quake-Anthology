/* Port of src/content/q3/base/game/spawn.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/spawn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/common_parse.h"
#include "qa/core/game_numeric.h"
#include "qa/foundation/support.h"
static char *dupstr(const char *v) { size_t n = strlen(v); char *d = malloc(n + 1); if (d) memcpy(d, v, n + 1); return d; }
static int lower_equal(const char *a, const char *b) {
  while (*a && *b) { int ca = (unsigned char)*a, cb = (unsigned char)*b; if (ca >= 'A' && ca <= 'Z') ca += 32; if (cb >= 'A' && cb <= 'Z') cb += 32; if (ca != cb) return 0; a++; b++; }
  return *a == *b;
}
static void set_owned(char **dst, const char *v) { free(*dst); *dst = dupstr(v); }
char *qa_q3_new_spawn_string(const char *value, qa_q3_game_memory *memory) {
  size_t len = strlen(value); qa_q3_game_memory_allocation alloc = qa_q3_game_memory_allocate(memory, (double)(len + 1));
  size_t output = 0;
  for (size_t i = 0; i <= len; i++) {
    int ch = i == len ? 0 : (unsigned char)value[i];
    if (ch == 92 && i < len) { i++; alloc.bytes[output++] = value[i] == 'n' ? 10 : 92; }
    else alloc.bytes[output++] = (uint8_t)ch;
  }
  return qa_q3_game_memory_allocation_read_string(alloc);
}
void qa_q3_spawn_variables_init(qa_q3_spawn_variables *vars, const qa_q3_spawn_pair *entries, size_t count) {
  if (count > QA_Q3_MAX_SPAWN_VARS) qa_range_error("G_ParseSpawnVars: MAX_SPAWN_VARS");
  size_t chars = 0;
  for (size_t i = 0; i < count; i++) {
    const char *vals[2] = {entries[i].key, entries[i].value};
    for (int k = 0; k < 2; k++) { if (strlen(vals[k]) >= QA_TOKEN_MAX) qa_range_error("Spawn token exceeds MAX_TOKEN_CHARS"); chars += strlen(vals[k]) + 1; if (chars > QA_Q3_MAX_SPAWN_VARS_CHARS) qa_range_error("G_AddSpawnVarToken: MAX_SPAWN_CHARS"); }
  }
  vars->character_count = chars; vars->count = count; vars->entries = malloc((count ? count : 1) * sizeof *vars->entries);
  if (!vars->entries) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) { vars->entries[i].key = dupstr(entries[i].key); vars->entries[i].value = dupstr(entries[i].value); }
}
void qa_q3_spawn_variables_destroy(qa_q3_spawn_variables *vars) {
  for (size_t i = 0; i < vars->count; i++) { free(vars->entries[i].key); free(vars->entries[i].value); }
  free(vars->entries); vars->entries = NULL; vars->count = 0;
}
qa_q3_spawn_value_string qa_q3_spawn_variables_string(const qa_q3_spawn_variables *vars, const char *key, const char *def) {
  for (size_t i = 0; i < vars->count; i++) if (lower_equal(vars->entries[i].key, key)) { qa_q3_spawn_value_string r = {true, vars->entries[i].value}; return r; }
  qa_q3_spawn_value_string r = {false, def}; return r;
}
qa_q3_spawn_value_number qa_q3_spawn_variables_int(const qa_q3_spawn_variables *vars, const char *key, const char *def) {
  qa_q3_spawn_value_string f = qa_q3_spawn_variables_string(vars, key, def); qa_q3_spawn_value_number r = {f.present, qa_game_atoi(f.value)}; return r;
}
qa_q3_spawn_value_number qa_q3_spawn_variables_float(const qa_q3_spawn_variables *vars, const char *key, const char *def) {
  qa_q3_spawn_value_string f = qa_q3_spawn_variables_string(vars, key, def); qa_q3_spawn_value_number r = {f.present, qa_game_atof(f.value)}; return r;
}
qa_q3_spawn_value_vector qa_q3_spawn_variables_vector(const qa_q3_spawn_variables *vars, const char *key, const char *def) {
  qa_q3_spawn_value_string f = qa_q3_spawn_variables_string(vars, key, def); qa_game_vec3 v = qa_scan_game_vector(f.value);
  qa_q3_spawn_value_vector r = {f.present, {v.x, v.y, v.z}}; return r;
}
void qa_q3_spawn_parser_init(qa_q3_spawn_parser *p, const char *text, const char *name) {
  p->text = text; p->name = name ? name : "<spawn entities>"; p->offset = 0; p->line = 1; p->column = 1;
}
static int token_ws(int code) { return code <= 32 || code >= 128; }
static void advance(qa_q3_spawn_parser *p) { if (p->text[p->offset] == '\n') { p->line++; p->column = 1; } else p->column++; p->offset++; }
typedef struct { char value[QA_TOKEN_MAX]; int present; } token_t;
static token_t next_token(qa_q3_spawn_parser *p) {
  size_t len = strlen(p->text);
  while (1) {
    while (p->offset < len && token_ws((unsigned char)p->text[p->offset])) advance(p);
    if (p->offset + 1 < len && p->text[p->offset] == '/' && p->text[p->offset + 1] == '/') { while (p->offset < len && p->text[p->offset] != '\n') advance(p); }
    else if (p->offset + 1 < len && p->text[p->offset] == '/' && p->text[p->offset + 1] == '*') {
      advance(p); advance(p);
      while (p->offset + 1 < len && !(p->text[p->offset] == '*' && p->text[p->offset + 1] == '/')) advance(p);
      if (p->offset < len) { advance(p); if (p->offset < len) advance(p); }
    } else break;
  }
  token_t t; t.present = 0; t.value[0] = 0; if (p->offset == len) return t;
  int quoted = p->text[p->offset] == '"'; size_t n = 0; if (quoted) advance(p);
  while (p->offset < len) {
    char ch = p->text[p->offset];
    if (quoted ? ch == '"' : token_ws((unsigned char)ch)) break;
    t.value[n++] = ch; advance(p); if (n >= QA_TOKEN_MAX) qa_range_error("Spawn token exceeds MAX_TOKEN_CHARS");
  }
  if (quoted && p->offset < len) advance(p);
  t.value[n] = 0; t.present = 1; return t;
}
bool qa_q3_spawn_parser_next(qa_q3_spawn_parser *p, qa_q3_spawn_variables *out) {
  token_t opening = next_token(p); if (!opening.present) return false;
  if (opening.value[0] != '{') qa_range_error("G_ParseSpawnVars: found %s when expecting {", opening.value);
  qa_q3_spawn_pair entries[QA_Q3_MAX_SPAWN_VARS]; size_t count = 0, chars = 0;
  while (1) {
    token_t key = next_token(p); if (!key.present) qa_range_error("G_ParseSpawnVars: EOF without closing brace");
    if (key.value[0] == '}') { qa_q3_spawn_variables_init(out, entries, count); for (size_t i = 0; i < count; i++) { free(entries[i].key); free(entries[i].value); } return true; }
    token_t value = next_token(p); if (!value.present) qa_range_error("G_ParseSpawnVars: EOF without closing brace");
    if (value.value[0] == '}') qa_range_error("G_ParseSpawnVars: closing brace without data");
    if (count == QA_Q3_MAX_SPAWN_VARS) qa_range_error("G_ParseSpawnVars: MAX_SPAWN_VARS");
    chars += strlen(key.value) + strlen(value.value) + 2;
    if (chars > QA_Q3_MAX_SPAWN_VARS_CHARS) qa_range_error("G_AddSpawnVarToken: MAX_SPAWN_CHARS");
    entries[count].key = dupstr(key.value); entries[count].value = dupstr(value.value); count++;
  }
}
bool qa_q3_parse_spawn_field(const char *key, const char *value, qa_q3_game_entity *e, qa_q3_game_memory *memory) {
  if (lower_equal(key, "classname")) { char *s = qa_q3_new_spawn_string(value, memory); qa_q3_game_entity_set_classname(e, s); free(s); }
  else if (lower_equal(key, "model")) { char *s = qa_q3_new_spawn_string(value, memory); set_owned(&e->model, s); free(s); }
  else if (lower_equal(key, "model2")) { char *s = qa_q3_new_spawn_string(value, memory); set_owned(&e->model2, s); free(s); }
  else if (lower_equal(key, "target")) { char *s = qa_q3_new_spawn_string(value, memory); set_owned(&e->target, s); free(s); }
  else if (lower_equal(key, "targetname")) { char *s = qa_q3_new_spawn_string(value, memory); set_owned(&e->targetname, s); free(s); }
  else if (lower_equal(key, "message")) { char *s = qa_q3_new_spawn_string(value, memory); set_owned(&e->message, s); free(s); }
  else if (lower_equal(key, "team")) { char *s = qa_q3_new_spawn_string(value, memory); set_owned(&e->team, s); free(s); }
  else if (lower_equal(key, "targetshadername")) { char *s = qa_q3_new_spawn_string(value, memory); set_owned(&e->target_shader_name, s); free(s); }
  else if (lower_equal(key, "targetshadernewname")) { char *s = qa_q3_new_spawn_string(value, memory); set_owned(&e->target_shader_new_name, s); free(s); }
  else if (lower_equal(key, "spawnflags")) e->spawnflags = qa_game_atoi(value);
  else if (lower_equal(key, "count")) e->count = qa_game_atoi(value);
  else if (lower_equal(key, "health")) qa_q3_game_entity_set_health(e, qa_game_atoi(value));
  else if (lower_equal(key, "dmg")) e->damage = qa_game_atoi(value);
  else if (lower_equal(key, "speed")) e->speed = qa_game_atof(value);
  else if (lower_equal(key, "wait")) e->wait = qa_game_atof(value);
  else if (lower_equal(key, "random")) e->random = qa_game_atof(value);
  else if (lower_equal(key, "origin")) { qa_game_vec3 v = qa_scan_game_vector(value); e->s.origin = (qa_vec3){v.x, v.y, v.z}; }
  else if (lower_equal(key, "angles")) { qa_game_vec3 v = qa_scan_game_vector(value); e->s.angles = (qa_vec3){v.x, v.y, v.z}; }
  else if (lower_equal(key, "angle")) e->s.angles = (qa_vec3){0, qa_game_atof(value), 0};
  else if (lower_equal(key, "light")) { }
  else return false;
  return true;
}
const char *qa_q3_spawn_filter_name(qa_q3_spawn_filter f) {
  switch (f) { case QA_Q3_SPAWN_NOTSINGLE: return "notsingle"; case QA_Q3_SPAWN_NOTTEAM: return "notteam";
    case QA_Q3_SPAWN_NOTFREE: return "notfree"; case QA_Q3_SPAWN_NOTTA: return "notta";
    case QA_Q3_SPAWN_NOTQ3A: return "notq3a"; default: return "gametype"; }
}
static int excluded(const qa_q3_spawn_variables *vars, qa_q3_spawn_context *ctx, qa_q3_spawn_filter *out) {
  if (ctx->game_type == 2 && qa_q3_spawn_variables_int(vars, "notsingle", "0").value != 0) { *out = QA_Q3_SPAWN_NOTSINGLE; return 1; }
  int team = ctx->game_type >= 3;
  if (qa_q3_spawn_variables_int(vars, team ? "notteam" : "notfree", "0").value != 0) { *out = team ? QA_Q3_SPAWN_NOTTEAM : QA_Q3_SPAWN_NOTFREE; return 1; }
  int mp = ctx->product == QA_Q3_PRODUCT_MISSIONPACK;
  if (qa_q3_spawn_variables_int(vars, mp ? "notta" : "notq3a", "0").value != 0) { *out = mp ? QA_Q3_SPAWN_NOTTA : QA_Q3_SPAWN_NOTQ3A; return 1; }
  qa_q3_spawn_value_string gt = qa_q3_spawn_variables_string(vars, "gametype", "");
  if (gt.present && ctx->game_type >= 0 && ctx->game_type < 8) {
    if (ctx->game_type != (int)ctx->game_type) qa_range_error("No source gametype name");
    static const char *names[8] = {"ffa", "tournament", "single", "team", "ctf", "oneflag", "obelisk", "harvester"};
    if (!strstr(gt.value, names[(int)ctx->game_type])) { *out = QA_Q3_SPAWN_GAMETYPE; return 1; }
  }
  return 0;
}
qa_q3_spawn_outcome qa_q3_spawn_entity(const qa_q3_spawn_variables *vars, qa_q3_spawn_context *ctx) {
  qa_q3_game_entity *e = qa_q3_entity_pool_spawn(ctx->pool); double slot = e->slot; qa_q3_spawn_outcome o; memset(&o, 0, sizeof o); o.slot = slot;
  for (size_t i = 0; i < vars->count; i++) qa_q3_parse_spawn_field(vars->entries[i].key, vars->entries[i].value, e, ctx->memory);
  qa_q3_spawn_filter reason;
  if (excluded(vars, ctx, &reason)) { qa_q3_entity_pool_free(ctx->pool, e); o.kind = QA_Q3_SPAWN_FILTERED; o.reason = reason; return o; }
  e->s.pos.base = e->s.origin; e->r.current_origin = e->s.origin;
  const char *classname = qa_q3_game_entity_classname(e); o.classname = classname;
  if (!classname) ctx->warn(ctx->ctx, "G_CallSpawn: NULL classname\n");
  else {
    for (size_t i = 0; i < ctx->item_count; i++) if (ctx->items[i].classname && strcmp(ctx->items[i].classname, classname) == 0) {
      ctx->spawn_item(ctx->ctx, e, &ctx->items[i], vars); o.kind = QA_Q3_SPAWN_DISPATCHED; o.route = QA_Q3_ROUTE_ITEM; o.entity = e; return o;
    }
    for (size_t i = 0; i < ctx->handler_count; i++) if (strcmp(ctx->handlers[i].classname, classname) == 0) {
      ctx->handlers[i].fn(ctx->handlers[i].ctx, e, vars); o.kind = QA_Q3_SPAWN_DISPATCHED; o.route = QA_Q3_ROUTE_HANDLER; o.entity = e; return o;
    }
    char buf[1024]; snprintf(buf, sizeof buf, "%s doesn't have a spawn function\n", classname); ctx->warn(ctx->ctx, buf);
  }
  qa_q3_entity_pool_free(ctx->pool, e); o.kind = QA_Q3_SPAWN_UNKNOWN; return o;
}
void qa_q3_spawn_world(const qa_q3_spawn_variables *vars, qa_q3_worldspawn_context *ctx) {
  if (!lower_equal(qa_q3_spawn_variables_string(vars, "classname", "").value, "worldspawn"))
    qa_range_error("SP_worldspawn: The first entity isn't 'worldspawn'");
  ctx->set_configstring(ctx->ctx, 20, "baseq3-1");
  char buf[64]; snprintf(buf, sizeof buf, "%lld", (long long)(int)ctx->start_time); ctx->set_configstring(ctx->ctx, 21, buf);
  ctx->set_configstring(ctx->ctx, 2, qa_q3_spawn_variables_string(vars, "music", "").value);
  ctx->set_configstring(ctx->ctx, 3, qa_q3_spawn_variables_string(vars, "message", "").value);
  ctx->set_configstring(ctx->ctx, 4, ctx->motd);
  ctx->set_cvar(ctx->ctx, "g_gravity", qa_q3_spawn_variables_string(vars, "gravity", "800").value);
  ctx->set_cvar(ctx->ctx, "g_enableDust", qa_q3_spawn_variables_string(vars, "enableDust", "0").value);
  ctx->set_cvar(ctx->ctx, "g_enableBreath", qa_q3_spawn_variables_string(vars, "enableBreath", "0").value);
  qa_q3_game_entity *world = qa_q3_entity_pool_at(ctx->pool, QA_Q3_ENTITYNUM_WORLD);
  world->s.number = QA_Q3_ENTITYNUM_WORLD; qa_q3_game_entity_set_classname(world, "worldspawn");
  ctx->set_configstring(ctx->ctx, 5, "");
  if (ctx->restarted != 0) { ctx->set_cvar(ctx->ctx, "g_restarted", "0"); ctx->warmup_time = 0; }
  else if (ctx->do_warmup != 0) { ctx->warmup_time = -1; ctx->set_configstring(ctx->ctx, 5, "-1"); ctx->log(ctx->ctx, "Warmup:\n"); }
}
void qa_q3_spawn_report_destroy(qa_q3_spawn_report *r) { qa_q3_spawn_variables_destroy(&r->world_variables); free(r->outcomes); r->outcomes = NULL; r->outcome_count = 0; }
qa_q3_spawn_report qa_q3_spawn_entities(const char *text, qa_q3_spawn_map_context *ctx, const char *source) {
  if (ctx->spawn.pool != ctx->world.pool) qa_range_error("Worldspawn and entity dispatch must share an entity pool");
  qa_q3_spawn_parser parser; qa_q3_spawn_parser_init(&parser, text, source ? source : "<spawn entities>");
  qa_q3_spawn_report report; memset(&report, 0, sizeof report);
  if (!qa_q3_spawn_parser_next(&parser, &report.world_variables)) qa_range_error("SpawnEntities: no entities");
  qa_q3_spawn_world(&report.world_variables, &ctx->world);
  size_t cap = 0;
  while (1) {
    qa_q3_spawn_variables vars; if (!qa_q3_spawn_parser_next(&parser, &vars)) break;
    if (report.outcome_count == cap) { cap = cap ? cap * 2 : 16; void *n = realloc(report.outcomes, cap * sizeof *report.outcomes); if (!n) qa_range_error("out of memory"); report.outcomes = n; }
    report.outcomes[report.outcome_count++] = qa_q3_spawn_entity(&vars, &ctx->spawn); qa_q3_spawn_variables_destroy(&vars);
  }
  return report;
}
