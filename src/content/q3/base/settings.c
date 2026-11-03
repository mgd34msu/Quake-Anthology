/* Port of src/content/q3/base/settings.ts. GPL-2.0-or-later.
 * Definition rows transcribed by bun JSON dump of q3GameCvarDefinitions. */
#include "qa/content/q3/base/settings.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/format.h"
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

static const qa_q3_base_cvar_definition BASE_DEFINITIONS[] = {
  {"sv_cheats","",0,false,false},
  {"g_restarted","0",64,false,false},
  {"g_gametype","0",38,false,false},
  {"sv_maxclients","8",37,false,false},
  {"g_maxGameClients","0",37,false,false},
  {"dmflags","0",5,true,false},
  {"fraglimit","20",1029,true,false},
  {"timelimit","0",1029,true,false},
  {"capturelimit","8",1029,true,false},
  {"g_synchronousClients","0",8,false,false},
  {"g_friendlyFire","0",1,true,false},
  {"g_teamAutoJoin","0",1,false,false},
  {"g_teamForceBalance","0",1,false,false},
  {"g_warmup","20",1,true,false},
  {"g_doWarmup","0",0,true,false},
  {"g_log","games.log",1,false,false},
  {"g_logSync","0",1,false,false},
  {"g_password","",2,false,false},
  {"g_banIPs","",1,false,false},
  {"g_filterBan","1",1,false,false},
  {"g_needpass","0",68,false,false},
  {"dedicated","0",0,false,false},
  {"g_speed","320",0,true,false},
  {"g_gravity","800",0,true,false},
  {"g_knockback","1000",0,true,false},
  {"g_quadfactor","3",0,true,false},
  {"g_weaponrespawn","5",0,true,false},
  {"g_weaponTeamRespawn","30",0,true,false},
  {"g_forcerespawn","20",0,true,false},
  {"g_inactivity","0",0,true,false},
  {"g_debugMove","0",0,false,false},
  {"g_debugDamage","0",0,false,false},
  {"g_debugAlloc","0",0,false,false},
  {"g_motd","",0,false,false},
  {"com_blood","1",0,false,false},
  {"g_podiumDist","80",0,false,false},
  {"g_podiumDrop","70",0,false,false},
  {"g_allowVote","1",1,false,false},
  {"g_listEntity","0",0,false,false},
  {"g_smoothClients","1",0,false,false},
  {"pmove_fixed","0",8,false,false},
  {"pmove_msec","8",8,false,false},
  {"pmove_ladders","1",8,false,false},
  {"g_rankings","0",0,false,false},
  {"sv_enableRankings","0",0,false,false},
  {"sv_rankingsActive","0",64,false,false},
  {"sv_rankingsGameKey","",68,false,false},
  {"sv_rankingsGameID","",68,false,false},
};
static const qa_q3_base_cvar_definition MISSIONPACK_DEFINITIONS[] = {
  {"sv_cheats","",0,false,false},
  {"g_restarted","0",64,false,false},
  {"g_gametype","0",38,false,false},
  {"sv_maxclients","8",37,false,false},
  {"g_maxGameClients","0",37,false,false},
  {"dmflags","0",5,true,false},
  {"fraglimit","20",1029,true,false},
  {"timelimit","0",1029,true,false},
  {"capturelimit","8",1029,true,false},
  {"g_synchronousClients","0",8,false,false},
  {"g_friendlyFire","0",1,true,false},
  {"g_teamAutoJoin","0",1,false,false},
  {"g_teamForceBalance","0",1,false,false},
  {"g_warmup","20",1,true,false},
  {"g_doWarmup","0",0,true,false},
  {"g_log","games.log",1,false,false},
  {"g_logSync","0",1,false,false},
  {"g_password","",2,false,false},
  {"g_banIPs","",1,false,false},
  {"g_filterBan","1",1,false,false},
  {"g_needpass","0",68,false,false},
  {"dedicated","0",0,false,false},
  {"g_speed","320",0,true,false},
  {"g_gravity","800",0,true,false},
  {"g_knockback","1000",0,true,false},
  {"g_quadfactor","3",0,true,false},
  {"g_weaponrespawn","5",0,true,false},
  {"g_weaponTeamRespawn","30",0,true,false},
  {"g_forcerespawn","20",0,true,false},
  {"g_inactivity","0",0,true,false},
  {"g_debugMove","0",0,false,false},
  {"g_debugDamage","0",0,false,false},
  {"g_debugAlloc","0",0,false,false},
  {"g_motd","",0,false,false},
  {"com_blood","1",0,false,false},
  {"g_podiumDist","80",0,false,false},
  {"g_podiumDrop","70",0,false,false},
  {"g_allowVote","1",1,false,false},
  {"g_listEntity","0",0,false,false},
  {"g_obeliskHealth","2500",0,false,false},
  {"g_obeliskRegenPeriod","1",0,false,false},
  {"g_obeliskRegenAmount","15",0,false,false},
  {"g_obeliskRespawnDelay","10",4,false,false},
  {"g_cubeTimeout","30",0,false,false},
  {"g_redteam","Stroggs",7,true,true},
  {"g_blueteam","Pagans",7,true,true},
  {"ui_singlePlayerActive","",0,false,false},
  {"g_enableDust","0",4,true,false},
  {"g_enableBreath","0",4,true,false},
  {"g_proxMineTimeout","20000",0,false,false},
  {"g_smoothClients","1",0,false,false},
  {"pmove_fixed","0",8,false,false},
  {"pmove_msec","8",8,false,false},
  {"pmove_ladders","1",8,false,false},
  {"g_rankings","0",0,false,false},
  {"sv_enableRankings","0",0,false,false},
  {"sv_rankingsActive","0",64,false,false},
  {"sv_rankingsGameKey","",68,false,false},
  {"sv_rankingsGameID","",68,false,false},
};
enum { BASE_COUNT = sizeof BASE_DEFINITIONS / sizeof BASE_DEFINITIONS[0],
       MISSIONPACK_COUNT = sizeof MISSIONPACK_DEFINITIONS / sizeof MISSIONPACK_DEFINITIONS[0] };
const qa_q3_base_cvar_definition *qa_q3_base_game_cvar_definitions(qa_q3_base_product product, size_t *out_count) {
  if (product == QA_Q3_BASE_PRODUCT_BASEQ3) { *out_count = BASE_COUNT; return BASE_DEFINITIONS; }
  *out_count = MISSIONPACK_COUNT; return MISSIONPACK_DEFINITIONS;
}

typedef struct snapshot_entry { char *name; qa_cvar_snapshot snapshot; } snapshot_entry;
struct qa_q3_base_game_settings {
  qa_q3_base_settings_host host;
  qa_q3_base_product product;
  const qa_q3_base_cvar_definition *definitions; size_t definition_count;
  snapshot_entry *snapshots; size_t snapshot_count, snapshot_cap;
};
static void *xmalloc(size_t n){ void *p = malloc(n ? n : 1); if (!p) qa_range_error("out of memory"); return p; }
static char *xstrdup(const char *s){ size_t n = strlen(s) + 1; char *p = xmalloc(n); memcpy(p, s, n); return p; }
static snapshot_entry *find_entry(qa_q3_base_game_settings *s, const char *name) {
  for (size_t i = 0; i < s->snapshot_count; i++) if (strcmp(s->snapshots[i].name, name) == 0) return &s->snapshots[i];
  return NULL;
}
/* JS Map.set: existing key keeps its position. Takes ownership of snapshot. */
static void set_snapshot(qa_q3_base_game_settings *s, const char *name, qa_cvar_snapshot snapshot) {
  snapshot_entry *e = find_entry(s, name);
  if (e) { qa_cvar_read_free(&e->snapshot); e->snapshot = snapshot; return; }
  if (s->snapshot_count == s->snapshot_cap) {
    s->snapshot_cap = s->snapshot_cap ? s->snapshot_cap * 2 : 16;
    snapshot_entry *grown = realloc(s->snapshots, s->snapshot_cap * sizeof *grown);
    if (!grown) qa_range_error("out of memory");
    s->snapshots = grown;
  }
  s->snapshots[s->snapshot_count].name = xstrdup(name);
  s->snapshots[s->snapshot_count].snapshot = snapshot;
  s->snapshot_count++;
}
static void clear_snapshots(qa_q3_base_game_settings *s) {
  for (size_t i = 0; i < s->snapshot_count; i++) { free(s->snapshots[i].name); qa_cvar_read_free(&s->snapshots[i].snapshot); }
  s->snapshot_count = 0;
}
qa_q3_base_game_settings *qa_q3_base_game_settings_create(qa_q3_base_settings_host host, qa_q3_base_product product) {
  qa_q3_base_game_settings *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->host = host; s->product = product;
  s->definitions = qa_q3_base_game_cvar_definitions(product, &s->definition_count);
  return s;
}
void qa_q3_base_game_settings_destroy(qa_q3_base_game_settings *s) {
  if (!s) return;
  clear_snapshots(s); free(s->snapshots); free(s);
}
const qa_q3_base_cvar_definition *qa_q3_base_game_settings_definitions(const qa_q3_base_game_settings *s, size_t *out_count) {
  *out_count = s->definition_count; return s->definitions;
}
qa_q3_module_cvar_capture *qa_q3_base_game_settings_capture_save_state(qa_q3_base_game_settings *s, size_t *out_count) {
  qa_q3_module_cvar_capture *out = malloc((s->snapshot_count ? s->snapshot_count : 1) * sizeof *out);
  if (!out) return NULL;
  for (size_t i = 0; i < s->snapshot_count; i++) {
    qa_cvar_snapshot *snap = &s->snapshots[i].snapshot;
    out[i] = qa_q3_capture_module_cvar(snap);
  }
  *out_count = s->snapshot_count;
  return out;
}
void qa_q3_base_game_settings_restore_save_state(qa_q3_base_game_settings *s, const qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "q3.settings")) qa_range_error("out of memory");
  size_t count = qa_save_reader_list_count(&reader);
  qa_cvar_snapshot *snapshots = xmalloc((count ? count : 1) * sizeof *snapshots);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&reader, i, &item)) qa_range_error("out of memory");
    snapshots[i] = qa_q3_read_module_cvar(&item);
    qa_save_reader_destroy(&item);
  }
  /* definitions map asciiFold(name) -> canonical name (last wins, JS Map). */
  typedef struct restored_entry { const char *name; qa_cvar_snapshot *snapshot; } restored_entry;
  restored_entry *restored = xmalloc((count ? count : 1) * sizeof *restored);
  size_t restored_count = 0;
  for (size_t i = 0; i < count; i++) {
    char *folded = qa_ascii_fold(snapshots[i].name);
    if (!folded) qa_range_error("out of memory");
    const char *name = NULL;
    for (size_t j = 0; j < s->definition_count; j++) {
      char *df = qa_ascii_fold(s->definitions[j].name);
      if (!df) qa_range_error("out of memory");
      bool eq = strcmp(df, folded) == 0;
      free(df);
      if (eq) name = s->definitions[j].name;
    }
    free(folded);
    bool dup = false;
    if (name) for (size_t j = 0; j < restored_count; j++) if (strcmp(restored[j].name, name) == 0) dup = true;
    if (!name || dup) qa_save_reader_fail(&reader, "invalid settings snapshot names");
    restored[restored_count].name = name;
    restored[restored_count].snapshot = &snapshots[i];
    restored_count++;
  }
  if (restored_count != s->definition_count) qa_save_reader_fail(&reader, "invalid settings snapshot names");
  clear_snapshots(s);
  for (size_t i = 0; i < restored_count; i++) {
    qa_cvar_snapshot *m = restored[i].snapshot, snap = *m;
    m->name = NULL; m->value = NULL; m->reset_value = NULL; m->latched_value = NULL; /* ownership moves */
    set_snapshot(s, restored[i].name, snap);
  }
  for (size_t i = 0; i < count; i++) qa_cvar_read_free(&snapshots[i]);
  free(snapshots); free(restored);
  qa_save_reader_destroy(&reader);
}
void qa_q3_base_game_settings_register(qa_q3_base_game_settings *s, const char *build_date) {
  for (size_t i = 0; i < s->definition_count; i++) {
    const qa_q3_base_cvar_definition *def = &s->definitions[i];
    if (strcmp(def->name, "g_restarted") == 0) {
      qa_cvar_snapshot ignored;
      (void)qa_cvar_registry_register(s->host.cvars, "gamename", "baseq3", QA_CVAR_FLAG_SERVER_INFO | QA_CVAR_FLAG_READ_ONLY, &ignored);
      qa_cvar_read_free(&ignored);
      (void)qa_cvar_registry_register(s->host.cvars, "gamedate", build_date, QA_CVAR_FLAG_READ_ONLY, &ignored);
      qa_cvar_read_free(&ignored);
    }
    qa_cvar_snapshot current;
    if (!qa_cvar_registry_register(s->host.cvars, def->name, def->value, def->flags, &current))
      qa_range_error("Could not register Q3 game cvar %s", def->name);
    set_snapshot(s, def->name, current);
  }
}
const qa_cvar_snapshot *qa_q3_base_game_settings_snapshot(qa_q3_base_game_settings *s, const char *name) {
  snapshot_entry *e = find_entry(s, name);
  if (!e) qa_range_error("Unregistered Q3 game cvar %s", name);
  return &e->snapshot;
}
double qa_q3_base_game_settings_integer(qa_q3_base_game_settings *s, const char *name) {
  return qa_q3_base_game_settings_snapshot(s, name)->integer_value;
}
double qa_q3_base_game_settings_number(qa_q3_base_game_settings *s, const char *name) {
  return (double)(float)qa_q3_base_game_settings_snapshot(s, name)->numeric_value;
}
const char *qa_q3_base_game_settings_string(qa_q3_base_game_settings *s, const char *name) {
  return qa_q3_base_game_settings_snapshot(s, name)->value;
}
void qa_q3_base_game_settings_update(qa_q3_base_game_settings *s) {
  bool remapped = false;
  for (size_t i = 0; i < s->definition_count; i++) {
    const qa_q3_base_cvar_definition *def = &s->definitions[i];
    double previous_count = qa_q3_base_game_settings_snapshot(s, def->name)->modification_count;
    qa_cvar_snapshot current;
    if (!qa_cvar_registry_get(s->host.cvars, def->name, &current))
      qa_range_error("Game cvar disappeared: %s", def->name);
    double current_count = current.modification_count;
    char *current_value = xstrdup(current.value);
    set_snapshot(s, def->name, current);
    if (previous_count == current_count) { free(current_value); continue; }
    if (def->track) {
      qa_q3_game_format_arg args[2] = {qa_q3_game_format_string(def->name), qa_q3_game_format_string(current_value)};
      char *command = qa_q3_game_format("print \"Server: %s changed to %s\\n\"", args, 2, 32000);
      if (!command) qa_range_error("out of memory");
      s->host.send_server_command(s->host.ctx, -1, command);
      free(command);
    }
    free(current_value);
    if (def->team_shader) remapped = true;
  }
  if (remapped) s->host.remap_teams(s->host.ctx);
}
