/* Port of src/content/q2/foundation/checkpoint.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/support.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* Process-lifetime string table: restored strings outlive the checkpoint
 * they were read from, as immutable TS strings do. */
static pthread_mutex_t intern_lock = PTHREAD_MUTEX_INITIALIZER;
static char **intern_table;
static size_t intern_count, intern_capacity;
static size_t intern_hash(const char *text) {
  size_t hash = 1469598103934665603u;
  for (const unsigned char *p = (const unsigned char *)text; *p != 0; p++) hash = (hash ^ *p) * 1099511628211u;
  return hash;
}
static void intern_insert(char **table, size_t capacity, char *text) {
  size_t index = intern_hash(text) & (capacity - 1);
  while (table[index] != NULL) index = (index + 1) & (capacity - 1);
  table[index] = text;
}
const char *qa_q2_intern(const char *text) {
  if (text == NULL) return NULL;
  pthread_mutex_lock(&intern_lock);
  if (intern_capacity != 0) {
    size_t index = intern_hash(text) & (intern_capacity - 1);
    for (; intern_table[index] != NULL; index = (index + 1) & (intern_capacity - 1))
      if (strcmp(intern_table[index], text) == 0) { pthread_mutex_unlock(&intern_lock); return intern_table[index]; }
  }
  if ((intern_count + 1) * 2 > intern_capacity) {
    size_t capacity = intern_capacity != 0 ? intern_capacity * 2 : 256;
    char **table = calloc(capacity, sizeof *table);
    if (table == NULL) qa_range_error("out of memory");
    for (size_t i = 0; i < intern_capacity; i++) if (intern_table[i] != NULL) intern_insert(table, capacity, intern_table[i]);
    free(intern_table);
    intern_table = table;
    intern_capacity = capacity;
  }
  char *copy = strdup(text);
  if (copy == NULL) qa_range_error("out of memory");
  intern_insert(intern_table, intern_capacity, copy);
  intern_count++;
  pthread_mutex_unlock(&intern_lock);
  return copy;
}

bool qa_q2_save_actor(const qa_actor_id *actor, qa_saved_actor_id *out) {
  if (actor == NULL) return false;
  *out = (qa_saved_actor_id){(double)actor->slot, (double)actor->generation};
  return true;
}

qa_owned_actor qa_q2_restore_actor(qa_q2_game_services *g, qa_saved_actor_id actor) {
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_saved(g->host->actors, actor, &owner))
    qa_range_error("Q2 checkpoint references missing actor %s:%s", QA_JS_NUMBER(actor.slot), QA_JS_NUMBER(actor.generation));
  return owner;
}

qa_q2_attack_checkpoint qa_q2_save_attack(qa_attack_provenance a) {
  qa_q2_attack_checkpoint out = {0};
  out.values = a;
  out.has_attacker = qa_q2_save_actor(a.attacker, &out.attacker);
  out.has_inflictor = qa_q2_save_actor(a.inflictor, &out.inflictor);
  out.has_originating_projectile = a.has_originating_projectile;
  if (a.has_originating_projectile)
    out.originating_projectile = (qa_saved_actor_id){(double)a.originating_projectile.slot, (double)a.originating_projectile.generation};
  out.values.attacker = NULL;
  out.values.inflictor = NULL;
  return out;
}

qa_attack_provenance qa_q2_restore_attack(qa_q2_attack_checkpoint in,
    qa_actor_id (*reference)(void *ctx, qa_saved_actor_id a), void *ctx,
    qa_actor_id *attacker_scratch, qa_actor_id *inflictor_scratch) {
  qa_attack_provenance out = in.values;
  /* The checkpoint may be a decoded tree freed after restore. */
  out.weapon = qa_q2_intern(out.weapon);
  out.weapon_provider = qa_q2_intern(out.weapon_provider);
  out.damage_powerup_owner = qa_q2_intern(out.damage_powerup_owner);
  out.combat_provider = qa_q2_intern(out.combat_provider);
  out.inventory_provider = qa_q2_intern(out.inventory_provider);
  out.movement_provider = qa_q2_intern(out.movement_provider);
  out.cause.death_type = qa_q2_intern(out.cause.death_type);
  out.cause.armor_effect = qa_q2_intern(out.cause.armor_effect);
  out.cause.hazard = qa_q2_intern(out.cause.hazard);
  out.cause.native.edition = qa_q2_intern(out.cause.native.edition);
  out.cause.native.game = qa_q2_intern(out.cause.native.game);
  if (in.has_attacker) { *attacker_scratch = reference(ctx, in.attacker); out.attacker = attacker_scratch; }
  if (in.has_inflictor) { *inflictor_scratch = reference(ctx, in.inflictor); out.inflictor = inflictor_scratch; }
  out.has_originating_projectile = in.has_originating_projectile;
  if (in.has_originating_projectile) out.originating_projectile = reference(ctx, in.originating_projectile);
  return out;
}
