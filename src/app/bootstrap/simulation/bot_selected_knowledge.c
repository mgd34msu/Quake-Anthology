/* Port of src/app/bootstrap/simulation/bot-selected-knowledge.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_selected_knowledge.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/bots/behavior/library/script_sources.h"
#include "qa/bots/behavior/library/weapons.h"
#include "qa/bots/behavior/library/weights.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/ui/common/legacy/script/preprocessor.h"

double qa_app_sim_q3_knowledge_max_range(double weapon) { return weapon == 1 ? 60 : weapon == 6 ? 768 : 4096; }
double qa_app_sim_q3_knowledge_priority(double damage, double reload_seconds) {
  return damage / qa_js_max(0.05, reload_seconds);
}

typedef struct reader_scope {
  const qa_bot_catalog_files_bots_a *files;
  qa_ui_script_global_defines *globals;
  qa_bot_script_sources sources;
  qa_bot_script_reader resolver;
  qa_weight_config_store_bots_a weights;
  qa_weapon_ai_bots_a reader;
  qa_app_sim_q3_weapon_knowledge result;
} reader_scope;

static const uint8_t *read_file(void *ctx, const char *path, size_t *out_len) {
  const qa_bot_catalog_files_bots_a *files = ctx;
  return files->read(files->ctx, path, out_len);
}
/* TS (_severity, text) => { throw new Error(text); } */
static void print_throws(void *ctx, int severity, const char *text) {
  (void)ctx; (void)severity; /* TS ignores both */
  qa_raise("Error", 0, 0, "%s", text);
}
static void common_print(void *ctx, const char *text) { (void)ctx; (void)text; } /* TS () => undefined */

/* TS finally { reader.shutdown(); sources.disposeResources(); memory.dispose(); } */
static void scope_close(void *arg) {
  reader_scope *s = arg;
  qa_weapon_ai_shutdown(&s->reader);
  qa_bot_script_sources_dispose_resources(&s->sources);
  qa_bot_memory_dispose(&s->sources.memory);
  qa_weapon_ai_bots_a_destroy(&s->reader);
  qa_weight_config_store_bots_a_destroy(&s->weights);
  qa_bot_script_sources_destroy(&s->sources);
  qa_ui_script_global_defines_destroy(s->globals);
}
static void scope_fail(void *arg) {
  reader_scope *s = arg;
  scope_close(s);
  qa_app_sim_q3_weapon_knowledge_free(&s->result);
  free(s);
}

qa_app_sim_q3_weapon_knowledge qa_app_sim_native_q3_weapon_knowledge(const qa_bot_catalog_files_bots_a *files) {
  reader_scope *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->files = files;
  s->globals = qa_ui_script_global_defines_create(NULL, NULL, NULL);
  if (s->globals == NULL) qa_range_error("Out of memory");
  qa_bot_script_sources_init(&s->sources);
  s->sources.globals = s->globals;
  s->sources.read_file = read_file; s->sources.read_ctx = (void *)files;
  s->sources.print = print_throws; s->sources.print_ctx = s;
  s->sources.common_print = common_print; s->sources.common_print_ctx = s;
  s->resolver = qa_bot_script_sources_reader(&s->sources);
  qa_weight_config_store_options weight_options;
  memset(&weight_options, 0, sizeof weight_options);
  weight_options.memory = &s->sources.memory;
  qa_weight_config_store_bots_a_init(&s->weights);
  qa_weight_config_store_bots_a_configure(&s->weights, &s->resolver, &weight_options);
  qa_weapon_ai_options options;
  memset(&options, 0, sizeof options);
  options.memory = &s->sources.memory;
  qa_weapon_ai_bots_a_init(&s->reader);
  qa_weapon_ai_bots_a_configure(&s->reader, &s->resolver, &s->weights, &options);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, scope_fail, s);
  qa_weapon_config_bots_a *config = NULL;
  if (qa_weapon_ai_setup(&s->reader, NULL) != QA_WEAPON_LOAD_NO_ERROR || (config = qa_weapon_ai_config(&s->reader)) == NULL)
    qa_raise("Error", 0, 0, "Selected Q3 bot weapon metadata is unavailable");
  qa_app_sim_q3_weapon_knowledge *out = &s->result;
  out->weapons = calloc(QA_Q3_WEAPON_ITEM_COUNT, sizeof *out->weapons);
  out->entries = calloc(QA_Q3_WEAPON_ITEM_COUNT, sizeof *out->entries);
  out->flags = calloc(QA_Q3_WEAPON_ITEM_COUNT, sizeof *out->flags);
  if (out->weapons == NULL || out->entries == NULL || out->flags == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++) {
    const qa_q3_weapon_item *binding = &qa_q3_weapon_items[i];
    qa_weapon_info_bots_a info;
    if (!qa_weapon_config_weapon(config, (int64_t)binding->weapon, &info) || !info.valid || info.projectile_info.damage <= 0) continue;
    size_t n = out->count;
    qa_weapon_entry *entry = &out->entries[n];
    memset(entry, 0, sizeof *entry); /* TS new WeaponEntry(): zero numbers */
    entry->name = binding->item;
    entry->number = (double)qa_js_shl(1, binding->weapon);
    entry->identity.is_q2_classname = false; entry->identity.bit = entry->number; /* { kind: "q1-bit", bit } */
    entry->damage = (double)info.projectile_info.damage * (double)info.projectile_count;
    entry->speed = info.speed;
    entry->min_range = info.projectile_info.radius;
    entry->max_range = binding->weapon == 1 ? 60 : binding->weapon == 6 ? 768 : 4096;
    entry->priority = entry->damage / qa_js_max(0.05, info.reload);
    entry->ammo_name = binding->ammo != NULL ? binding->ammo : "";
    entry->ammo = entry->ammo_name;
    entry->min_ammo = info.ammo_amount; entry->max_ammo = 200;
    size_t flags = 0;
    out->flags[n][flags++] = binding->weapon == 1 ? QA_WEAPON_FLAG_MELEE : info.speed == 0 ? QA_WEAPON_FLAG_HITSCAN : QA_WEAPON_FLAG_PROJECTILE;
    if (info.projectile_info.radius > 0) out->flags[n][flags++] = QA_WEAPON_FLAG_EXPLOSIVE;
    if (info.projectile_info.gravity > 0) out->flags[n][flags++] = QA_WEAPON_FLAG_PARABOLIC;
    entry->flags = out->flags[n]; entry->flag_count = flags;
    entry->aim_point = info.projectile_info.radius > 0 ? "feet" : "center";
    out->weapons[n].entry = entry;
    out->weapons[n].is_melee = binding->weapon == 1;
    out->weapons[n].is_electric = false;
    out->weapons[n].needs_ammo = binding->ammo != NULL;
    out->count++;
  }
  qa_cleanup_pop(&cleanup);
  qa_app_sim_q3_weapon_knowledge result = s->result;
  scope_close(s);
  free(s);
  return result;
}

void qa_app_sim_q3_weapon_knowledge_free(qa_app_sim_q3_weapon_knowledge *knowledge) {
  free(knowledge->weapons); free(knowledge->entries); free(knowledge->flags);
  memset(knowledge, 0, sizeof *knowledge);
}
