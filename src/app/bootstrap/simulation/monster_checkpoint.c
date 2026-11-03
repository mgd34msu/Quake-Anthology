/* Port of src/app/bootstrap/simulation/monster-checkpoint.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/monster_checkpoint.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/persistence/q2_base_entities.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/q2_items.h"
#include "qa/persistence/q2_missionpacks.h"
#include "qa/persistence/q2_monsters.h"
#include "qa/persistence/q2_movers.h"
#include "qa/persistence/recipe.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"

static qa_save_reader fld(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_save_reader_fail(reader, "out of memory");
  return out;
}
static const char *str_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = fld(reader, name);
  const char *value = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double finite_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = fld(reader, name);
  double value = qa_save_reader_finite(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double integer_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = fld(reader, name);
  double value = qa_save_reader_integer(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double integer_min_of(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader f = fld(reader, name);
  double value = qa_save_reader_integer_min(&f, minimum);
  qa_save_reader_destroy(&f);
  return value;
}
static bool boolean_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = fld(reader, name);
  bool value = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double literal_of(const qa_save_reader *reader, const char *name, double expected) {
  qa_save_reader f = fld(reader, name);
  double value = qa_save_reader_literal_number(&f, expected);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *choice_of(const qa_save_reader *reader, const char *name, const char *const *choices, size_t count) {
  qa_save_reader f = fld(reader, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static bool undefined_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = fld(reader, name);
  bool undefined = qa_save_reader_is_undefined(&f);
  qa_save_reader_destroy(&f);
  return undefined;
}
static qa_saved_actor_id saved_actor_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = fld(reader, name);
  qa_saved_actor_id value = qa_read_saved_actor(&f);
  qa_save_reader_destroy(&f);
  return value;
}
/* TS field(name).nullable(readSavedActor). */
static bool nullable_actor_of(const qa_save_reader *reader, const char *name, qa_saved_actor_id *out) {
  qa_save_reader f = fld(reader, name);
  bool present = !qa_save_reader_is_null(&f);
  if (present) *out = qa_read_saved_actor(&f);
  qa_save_reader_destroy(&f);
  return present;
}
static qa_vec3 vector_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = fld(reader, name);
  qa_vec3 value = qa_read_vector(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_provider_reference provider_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = fld(reader, name);
  qa_provider_reference value = qa_read_provider(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_frame_context frame_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = fld(reader, name);
  qa_frame_context value = qa_read_frame(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static void free_random(qa_random_state *random) {
  free((void *)random->words.values);
  random->words.values = NULL;
  random->words.count = 0;
}
/* readRandom, then the source/foreign stream admission. */
static qa_random_state random_of(const qa_save_reader *reader, const char *message) {
  qa_save_reader f = fld(reader, "random");
  qa_random_state random = qa_read_random(&f);
  if (random.kind != QA_RANDOM_STATE_GLIBC_RANDOM && random.kind != QA_RANDOM_STATE_Q2_RERELEASE_MT19937) {
    free_random(&random);
    qa_save_reader_fail(&f, "%s", message);
  }
  qa_save_reader_destroy(&f);
  return random;
}
/* { blasterCauses: field("blasterCauses").list(...) } of field("ballistics"). */
static qa_app_sim_ballistics_checkpoint ballistics_of(const qa_save_reader *reader, qa_persist_decoded *owner) {
  qa_app_sim_ballistics_checkpoint out;
  memset(&out, 0, sizeof out);
  qa_save_reader ballistics = fld(reader, "ballistics");
  qa_save_reader causes = fld(&ballistics, "blasterCauses");
  size_t count = qa_save_reader_list_count(&causes);
  qa_q2w_cause_entry *entries = qa_persist_decoded_array(owner, count, sizeof *entries);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader cause = item(&causes, i);
    entries[i].actor = saved_actor_of(&cause, "actor");
    entries[i].means_of_death = integer_of(&cause, "meansOfDeath");
    qa_save_reader_destroy(&cause);
  }
  qa_save_reader_destroy(&causes);
  qa_save_reader_destroy(&ballistics);
  out.blaster_causes = entries;
  out.blaster_cause_count = count;
  return out;
}
static qa_q2mpm_monsters_checkpoint pack_monsters_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = fld(reader, name);
  qa_q2mpm_monsters_checkpoint value;
  memset(&value, 0, sizeof value);
  if (!qa_read_q2_mission_pack_monsters_checkpoint(&f, &value)) qa_save_reader_fail(&f, "out of memory");
  qa_save_reader_destroy(&f);
  return value;
}

static const char *const editions[] = {"classic", "rerelease"};
static const char *const packs[] = {"xatrix", "rogue"};

qa_app_sim_foreign_q2_world_checkpoint qa_app_sim_read_foreign_q2_world_checkpoint(const qa_save_reader *reader) {
  static const char *const programs[] = {"baseq2", "xatrix", "rogue", "mg2"};
  qa_app_sim_foreign_q2_world_checkpoint out;
  memset(&out, 0, sizeof out);
  out.random = random_of(reader, "Unsupported foreign world random stream");
  out.version = literal_of(reader, "version", 1);
  out.reference = provider_of(reader, "reference");
  out.edition = choice_of(reader, "edition", editions, 2);
  out.program = choice_of(reader, "program", programs, 4);
  out.frame = frame_of(reader, "frame");
  qa_save_reader f = fld(reader, "entities");
  out.entities = qa_read_q2_foundation_checkpoint(&f, &out.owner);
  qa_save_reader_destroy(&f);
  f = fld(reader, "monsters");
  out.monsters = qa_read_q2_monsters_checkpoint(&f, &out.owner);
  qa_save_reader_destroy(&f);
  out.ballistics = ballistics_of(reader, &out.owner);
  f = fld(reader, "movers");
  out.movers = qa_read_q2_movers_checkpoint(&f, &out.owner);
  qa_save_reader_destroy(&f);
  f = fld(reader, "items");
  out.items = qa_read_q2_items_checkpoint(&f, &out.owner);
  qa_save_reader_destroy(&f);
  f = fld(reader, "baseEntities");
  out.base_entities = qa_read_q2_base_entities_checkpoint(&f, &out.owner);
  qa_save_reader_destroy(&f);
  f = fld(reader, "packs");
  out.pack_count = qa_save_reader_list_count(&f);
  out.packs = qa_persist_decoded_array(&out.owner, out.pack_count, sizeof *out.packs);
  for (size_t i = 0; i < out.pack_count; i++) {
    qa_save_reader pack = item(&f, i);
    out.packs[i].pack = choice_of(&pack, "pack", packs, 2);
    out.packs[i].monsters = pack_monsters_of(&pack, "monsters");
    out.packs[i].steam_id = integer_of(&pack, "steamId");
    qa_save_reader_destroy(&pack);
  }
  qa_save_reader_destroy(&f);
  return out;
}

void qa_app_sim_foreign_q2_world_checkpoint_free(qa_app_sim_foreign_q2_world_checkpoint *checkpoint) {
  if (checkpoint == NULL) return;
  for (size_t i = 0; i < checkpoint->pack_count; i++) qa_q2mpm_monsters_checkpoint_free(&checkpoint->packs[i].monsters);
  free_random(&checkpoint->random);
  qa_persist_decoded_free(&checkpoint->owner);
  checkpoint->packs = NULL;
  checkpoint->pack_count = 0;
}

qa_app_sim_foreign_q1_world_checkpoint qa_app_sim_read_foreign_q1_world_checkpoint(const qa_save_reader *reader) {
  static const char *const programs[] = {"id1", "hipnotic", "rogue", "dopa", "mg1", "mg3", "ctf"};
  static const double skills[] = {0, 1, 2, 3};
  qa_app_sim_foreign_q1_world_checkpoint out;
  memset(&out, 0, sizeof out);
  out.random = random_of(reader, "Unsupported foreign world random stream");
  out.version = literal_of(reader, "version", 1);
  out.reference = provider_of(reader, "reference");
  out.edition = choice_of(reader, "edition", editions, 2);
  out.program = choice_of(reader, "program", programs, 7);
  out.frame = frame_of(reader, "frame");
  qa_save_reader f = fld(reader, "entities");
  out.entities = qa_read_q1_foundation_checkpoint(&f);
  qa_save_reader_destroy(&f);
  if (!undefined_field(reader, "adjunct")) {
    qa_save_reader adjunct = fld(reader, "adjunct");
    out.has_adjunct = true;
    out.adjunct.flags = integer_of(&adjunct, "flags");
    qa_save_reader skill = fld(&adjunct, "skill");
    out.adjunct.skill = qa_save_reader_choice_number(&skill, skills, 4);
    qa_save_reader_destroy(&skill);
    qa_save_reader cvars = fld(&adjunct, "cvars");
    out.adjunct.cvar_count = qa_save_reader_list_count(&cvars);
    out.adjunct.cvars = qa_persist_decoded_array(&out.owner, out.adjunct.cvar_count, sizeof *out.adjunct.cvars);
    for (size_t i = 0; i < out.adjunct.cvar_count; i++) {
      qa_save_reader entry = item(&cvars, i);
      out.adjunct.cvars[i].name = str_of(&entry, "name");
      out.adjunct.cvars[i].value = str_of(&entry, "value");
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&cvars);
    qa_save_reader_destroy(&adjunct);
  }
  return out;
}

void qa_app_sim_foreign_q1_world_checkpoint_free(qa_app_sim_foreign_q1_world_checkpoint *checkpoint) {
  if (checkpoint == NULL) return;
  qa_q1_foundation_checkpoint_free(&checkpoint->entities);
  free_random(&checkpoint->random);
  qa_persist_decoded_free(&checkpoint->owner);
  checkpoint->adjunct.cvars = NULL;
  checkpoint->adjunct.cvar_count = 0;
}

static void read_authored(const qa_save_reader *value, qa_app_sim_saved_authored_monster *out, qa_persist_decoded *owner) {
  static const char *const activations[] = {"active", "dormant", "scheduled"};
  static const char *const placements[] = {"ready", "waiting", "teleport"};
  qa_save_reader activation = fld(value, "activation");
  const char *kind = choice_of(&activation, "kind", activations, 3);
  qa_save_reader placement = fld(value, "placement");
  const char *placement_kind = qa_save_reader_is_undefined(&placement) ? "ready" : choice_of(&placement, "kind", placements, 3);
  memset(out, 0, sizeof *out);
  out->actor = saved_actor_of(value, "actor");
  qa_save_reader definition = fld(value, "definition");
  out->definition.source = provider_of(&definition, "source");
  out->definition.classname = str_of(&definition, "classname");
  qa_save_reader_destroy(&definition);
  out->classname = str_of(value, "classname");
  out->source_ordinal = integer_min_of(value, "sourceOrdinal", 0);
  out->spawnflags = integer_of(value, "spawnflags");
  out->targetname = str_of(value, "targetname");
  out->target = str_of(value, "target");
  out->killtarget = str_of(value, "killtarget");
  out->message = str_of(value, "message");
  out->delay = finite_of(value, "delay");
  out->death_target = str_of(value, "deathTarget");
  out->drop_item = str_of(value, "dropItem");
  out->route = str_of(value, "route");
  out->has_route_goal = nullable_actor_of(value, "routeGoal", &out->route_goal);
  out->route_resolved = boolean_of(value, "routeResolved");
  out->counted_death = boolean_of(value, "countedDeath");
  out->combat_target = str_of(value, "combatTarget");
  out->has_combat_goal = nullable_actor_of(value, "combatGoal", &out->combat_goal);
  out->stand_ground = boolean_of(value, "standGround");
  if (strcmp(placement_kind, "waiting") == 0) {
    out->placement.kind = "waiting";
    qa_save_reader barriers = fld(&placement, "barriers");
    size_t count = qa_save_reader_list_count(&barriers);
    qa_app_sim_saved_monster_barrier *rows = qa_persist_decoded_array(owner, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader barrier = item(&barriers, i);
      rows[i].actor = saved_actor_of(&barrier, "actor");
      rows[i].origin = vector_of(&barrier, "origin");
      qa_save_reader_destroy(&barrier);
    }
    qa_save_reader_destroy(&barriers);
    out->placement.barriers = rows;
    out->placement.barrier_count = count;
    out->placement.has_activator = nullable_actor_of(&placement, "activator", &out->placement.activator);
  } else if (strcmp(placement_kind, "teleport") == 0) {
    out->placement.kind = "teleport";
    out->placement.origin = vector_of(&placement, "origin");
  } else out->placement.kind = "ready";
  out->activation.kind = kind;
  if (strcmp(kind, "scheduled") == 0) {
    out->activation.at = finite_of(&activation, "at");
    out->activation.has_activator = nullable_actor_of(&activation, "activator", &out->activation.activator);
  }
  qa_save_reader_destroy(&placement);
  qa_save_reader_destroy(&activation);
}

static void read_source(const qa_save_reader *value, qa_app_sim_monster_source_checkpoint *out, qa_persist_decoded *owner) {
  static const char *const kinds[] = {"q1", "q2"};
  memset(out, 0, sizeof *out);
  out->reference = provider_of(value, "reference");
  out->frame = frame_of(value, "frame");
  out->random = random_of(value, "Unsupported selected monster random stream");
  out->kind = choice_of(value, "kind", kinds, 2);
  qa_save_reader f = fld(value, "entities");
  if (strcmp(out->kind, "q1") == 0) {
    out->q1_entities = qa_read_q1_foundation_checkpoint(&f);
    qa_save_reader_destroy(&f);
    return;
  }
  out->q2_entities = qa_read_q2_foundation_checkpoint(&f, owner);
  qa_save_reader_destroy(&f);
  f = fld(value, "monsters");
  out->monsters = qa_read_q2_monsters_checkpoint(&f, owner);
  qa_save_reader_destroy(&f);
  f = fld(value, "movers");
  out->has_movers = !qa_save_reader_is_undefined(&f);
  if (out->has_movers) out->movers = qa_read_q2_movers_checkpoint(&f, owner);
  qa_save_reader_destroy(&f);
  f = fld(value, "packs");
  if (!qa_save_reader_is_undefined(&f)) {
    out->pack_count = qa_save_reader_list_count(&f);
    out->packs = qa_persist_decoded_array(owner, out->pack_count, sizeof *out->packs);
    for (size_t i = 0; i < out->pack_count; i++) {
      qa_save_reader pack = item(&f, i);
      out->packs[i].pack = choice_of(&pack, "pack", packs, 2);
      out->packs[i].state = pack_monsters_of(&pack, "state");
      qa_save_reader_destroy(&pack);
    }
  }
  qa_save_reader_destroy(&f);
  out->ballistics = ballistics_of(value, owner);
}

qa_app_sim_selected_monsters_checkpoint qa_app_sim_read_selected_monsters_checkpoint(const qa_save_reader *reader) {
  qa_app_sim_selected_monsters_checkpoint out;
  memset(&out, 0, sizeof out);
  out.version = literal_of(reader, "version", 2);
  qa_save_reader f = fld(reader, "authored");
  out.authored_count = qa_save_reader_list_count(&f);
  out.authored = qa_persist_decoded_array(&out.owner, out.authored_count, sizeof *out.authored);
  for (size_t i = 0; i < out.authored_count; i++) {
    qa_save_reader value = item(&f, i);
    read_authored(&value, &out.authored[i], &out.owner);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&f);
  f = fld(reader, "sources");
  out.source_count = qa_save_reader_list_count(&f);
  out.sources = qa_persist_decoded_array(&out.owner, out.source_count, sizeof *out.sources);
  for (size_t i = 0; i < out.source_count; i++) {
    qa_save_reader value = item(&f, i);
    read_source(&value, &out.sources[i], &out.owner);
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&f);
  return out;
}

void qa_app_sim_selected_monsters_checkpoint_free(qa_app_sim_selected_monsters_checkpoint *checkpoint) {
  if (checkpoint == NULL) return;
  for (size_t i = 0; i < checkpoint->source_count; i++) {
    qa_app_sim_monster_source_checkpoint *source = &checkpoint->sources[i];
    free_random(&source->random);
    if (strcmp(source->kind, "q1") == 0) qa_q1_foundation_checkpoint_free(&source->q1_entities);
    for (size_t j = 0; j < source->pack_count; j++) qa_q2mpm_monsters_checkpoint_free(&source->packs[j].state);
  }
  qa_persist_decoded_free(&checkpoint->owner);
  checkpoint->authored = NULL; checkpoint->authored_count = 0;
  checkpoint->sources = NULL; checkpoint->source_count = 0;
}
