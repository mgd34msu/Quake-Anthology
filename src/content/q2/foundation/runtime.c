/* Port of src/content/q2/foundation/runtime.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/runtime.h"
#include "qa/foundation/js.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/fields.h"
#include "qa/foundation/support.h"

typedef struct admission_entry { qa_q2_foundation *f; qa_q2_monster_admission a; } admission_entry;
static admission_entry *admissions;
static size_t admission_count, admission_cap;
static const qa_q2_monster_admission *find_admission(qa_q2_foundation *f) {
  for (size_t i = 0; i < admission_count; i++) if (admissions[i].f == f) return &admissions[i].a;
  return NULL;
}
void qa_q2_foundation_set_monster_admission(qa_q2_foundation *f, const qa_q2_monster_admission *a) {
  for (size_t i = 0; i < admission_count; i++)
    if (admissions[i].f == f) {
      if (a) admissions[i].a = *a;
      else { memmove(&admissions[i], &admissions[i + 1], (admission_count - i - 1) * sizeof *admissions); admission_count--; }
      return;
    }
  if (!a) return;
  if (admission_count == admission_cap) { admission_cap = admission_cap ? admission_cap * 2 : 8; admissions = realloc(admissions, admission_cap * sizeof *admissions); if (!admissions) qa_range_error("out of memory"); }
  admissions[admission_count++] = (admission_entry){f, *a};
}

typedef struct report_store {
  qa_q2_foundation *f;
  const qa_q2_spawn_fields **inhibited, **removed;
  qa_q2_entity **spawned;
  qa_owned_actor *replaced;
  qa_q2_spawn_fields *fields; size_t field_count;
} report_store;
static report_store *stores;
static size_t store_count, store_cap;
static report_store *store_for(qa_q2_foundation *f) {
  for (size_t i = 0; i < store_count; i++) if (stores[i].f == f) return &stores[i];
  if (store_count == store_cap) { store_cap = store_cap ? store_cap * 2 : 8; stores = realloc(stores, store_cap * sizeof *stores); if (!stores) qa_range_error("out of memory"); }
  stores[store_count] = (report_store){0};
  stores[store_count].f = f;
  return &stores[store_count++];
}

static bool kept(const double *keep, size_t n, double ordinal) {
  if (!keep || ordinal == 0) return true;
  for (size_t i = 0; i < n; i++) if (keep[i] == ordinal) return true;
  return false;
}

static double slot_of(qa_q2_foundation *f, qa_actor_id id) {
  bool has = false;
  double v = qa_q2_entity_services_source_slot(f, id, &has);
  return has ? v : 0;
}

static void find_teams(qa_q2_foundation *f) {
  qa_q2_game_services *g = qa_q2_entity_services_game(f);
  size_t n = 0;
  qa_q2_entity *const *all = qa_q2_entity_services_entities(f, &n);
  qa_q2_entity **sorted = NULL;
  if (n) { sorted = malloc(n * sizeof *sorted); if (!sorted) qa_range_error("out of memory"); memcpy(sorted, all, n * sizeof *sorted); }
  for (size_t i = 1; i < n; i++) {
    qa_q2_entity *key = sorted[i]; size_t j = i;
    while (j > 0 && slot_of(f, sorted[j - 1]->actor.id) > slot_of(f, key->actor.id)) { sorted[j] = sorted[j - 1]; j--; }
    sorted[j] = key;
  }
  for (size_t mi = 0; mi < n; mi++) {
    qa_q2_entity *master = sorted[mi];
    const char *name = qa_q2_spawn_get(&master->spawn, "team");
    if (!name || !name[0] || (qa_js_to_int32(master->flags) & 0x400) != 0) continue;
    master->has_team_master = true; master->team_master = master->actor.id;
    if (g->options.edition == QA_Q2_EDITION_RERELEASE) master->flags = (double)(qa_js_to_int32(master->flags) | 0x4000000);
    qa_q2_entity *chain = master;
    for (size_t i = 0; i < n; i++) {
      qa_q2_entity *member = sorted[i];
      const char *tn = qa_q2_spawn_get(&member->spawn, "team");
      if (member == master || slot_of(f, member->actor.id) <= slot_of(f, master->actor.id) || !tn || strcmp(tn, name) != 0 || (qa_js_to_int32(member->flags) & 0x400) != 0) continue;
      chain->has_team_chain = true; chain->team_chain = member->actor.id;
      member->has_team_master = true; member->team_master = master->actor.id;
      member->flags = (double)(qa_js_to_int32(member->flags) | 0x400);
      chain = member;
    }
  }
  if (g->options.edition == QA_Q2_EDITION_RERELEASE) {
    for (size_t mi = 0; mi < n; mi++) {
      qa_q2_entity *master = sorted[mi];
      const char *name = qa_q2_spawn_get(&master->spawn, "team");
      if (!name || !name[0] || strcmp(master->classname, "func_train") != 0 || (qa_js_to_int32(master->spawnflags) & 8) == 0 || (qa_js_to_int32(master->flags) & 0x400) == 0) continue;
      master->has_team_master = true; master->team_master = master->actor.id;
      master->has_team_chain = false;
      master->flags = (double)((qa_js_to_int32(master->flags) & ~0x400) | 0x4000000);
      qa_q2_entity *chain = master;
      for (size_t i = 0; i < n; i++) {
        qa_q2_entity *member = sorted[i];
        const char *tn = qa_q2_spawn_get(&member->spawn, "team");
        if (member == master || !tn || strcmp(tn, name) != 0) continue;
        chain->has_team_chain = true; chain->team_chain = member->actor.id;
        member->has_team_master = true; member->team_master = master->actor.id;
        member->has_team_chain = false;
        member->flags = (double)((qa_js_to_int32(member->flags) & ~0x4000000) | 0x400);
        member->speed = master->speed;
        g->motion(g->ctx, member, "push");
        chain = member;
      }
    }
  }
  free(sorted);
}

qa_q2_spawn_report qa_q2_foundation_load(qa_q2_foundation *f, const char *source, const double *keep, size_t keep_count) {
  qa_q2_game_services *g = qa_q2_entity_services_game(f);
  report_store *st = store_for(f);
  qa_q2_entities_free(st->fields, st->field_count);
  free(st->inhibited); free(st->removed); free(st->spawned); free(st->replaced);
  *st = (report_store){.f = f};
  qa_q2_spawn_fields *fields = NULL;
  size_t count = 0;
  if (!qa_q2_parse_entities(source, g->options.edition, &fields, &count)) qa_range_error("out of memory");
  st->fields = fields; st->field_count = count;
  const qa_q2_monster_admission *admission = find_admission(f);
  qa_monster_definition_reference *replacements = NULL;
  bool *has_replacement = NULL;
  if (count) {
    replacements = calloc(count, sizeof *replacements);
    has_replacement = calloc(count, sizeof *has_replacement);
    if (!replacements || !has_replacement) qa_range_error("out of memory");
  }
  if (admission) {
    for (size_t i = 0; i < count; i++) {
      if (!kept(keep, keep_count, fields[i].ordinal)) continue;
      if (qa_q2_inhibit_spawn(&fields[i], g->options) || (g->options.mode == QA_Q2_MODE_DEATHMATCH && strncmp(fields[i].classname, "monster_", 8) == 0)) continue;
      if (admission->resolve(admission->ctx, fields[i].classname, &fields[i], &replacements[i])) has_replacement[i] = true;
    }
  }
  size_t inh_n = 0, rem_n = 0, sp_n = 0, rep_n = 0;
  st->inhibited = malloc((count ? count : 1) * sizeof *st->inhibited);
  st->removed = malloc((count ? count : 1) * sizeof *st->removed);
  st->spawned = malloc((count ? count : 1) * sizeof *st->spawned);
  st->replaced = malloc((count ? count : 1) * sizeof *st->replaced);
  if (!st->inhibited || !st->removed || !st->spawned || !st->replaced) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    if (!kept(keep, keep_count, fields[i].ordinal)) continue;
    if (qa_q2_inhibit_spawn(&fields[i], g->options)) { st->inhibited[inh_n++] = &fields[i]; continue; }
    if (has_replacement[i] && admission) {
      qa_monster_definition_reference def = replacements[i];
      char buf[512];
      snprintf(buf, sizeof buf, "%s/%s", def.source.provider ? def.source.provider : "", def.classname);
      /* definition string must outlive allocate call only (registry copies per landed impl contract use) */
      qa_owned_actor actor = qa_q2_entity_services_allocate_actor(f, &fields[i], buf);
      qa_combat_state initial = {0};
      initial.health = 0; initial.mass = 100; initial.can_take_damage = false;
      initial.armor.regular.kind = QA_REGULAR_ARMOR_NONE; initial.armor.powered.kind = "none";
      qa_gameplay_authority_create_state(g->host->combat, actor, initial, NULL, NULL);
      admission->spawn(admission->ctx, actor, &fields[i], def);
      st->replaced[rep_n++] = actor;
      continue;
    }
    qa_q2_spawn_fields clone;
    if (!qa_q2_spawn_fields_clone(&clone, &fields[i])) qa_range_error("out of memory");
    /* TS reads entity.actor after spawn even when the source removed it; the C
     * entity is freed with its actor, so the id is taken before spawning. */
    qa_owned_actor spawned_actor = qa_q2_entity_services_allocate_actor(f, &clone, NULL);
    qa_q2_entity *entity = qa_q2_entity_services_spawn_entity(f, qa_q2_entity_services_attach(f, spawned_actor, clone));
    if (qa_session_actor_registry_is_live(g->host->actors, spawned_actor.id)) st->spawned[sp_n++] = entity;
    else st->removed[rem_n++] = &fields[i];
  }
  free(replacements); free(has_replacement);
  find_teams(f);
  size_t un_n = 0;
  qa_q2_entity *const *un = qa_q2_entity_services_unsupported(f, &un_n);
  return (qa_q2_spawn_report){.authored = (double)count, .inhibited = st->inhibited, .inhibited_count = inh_n,
      .removed_by_source = st->removed, .removed_count = rem_n, .spawned = st->spawned, .spawned_count = sp_n,
      .unsupported = un, .unsupported_count = un_n, .replaced = st->replaced, .replaced_count = rep_n};
}
