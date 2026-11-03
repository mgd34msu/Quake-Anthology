/* Port of src/content/q1/quakec/id1-projectiles.ts. GPL-2.0-or-later. */
#include "qa/content/q1/quakec/id1_projectiles.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/quakec/id1_program.h"
#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *value = malloc(size != 0 ? size : 1);
  if (value == NULL) qa_range_error("out of memory");
  return value;
}
static void *xrealloc(void *ptr, size_t size) {
  void *value = realloc(ptr, size != 0 ? size : 1);
  if (value == NULL) qa_range_error("out of memory");
  return value;
}
static noreturn void proj_error(const char *message) { qa_q1qc_error("progs.dat", "%s", message); }

typedef struct emission { qa_actor_id owner; double emitted_at; qa_item_id weapon; } emission;
typedef struct emission_entry { qa_actor_id actor; emission value; } emission_entry;
typedef struct weapon_entry { int64_t function_index; qa_item_id weapon; } weapon_entry;
typedef struct site_entry { int64_t statement, caller; } site_entry;
struct qa_q1_id1_projectile_attacks {
  qa_q1qc_source source;
  qa_q1qc_foreign_source foreign;
  qa_q1qc_machine_fn machine;
  void *machine_ctx;
  emission_entry *emissions; size_t emission_count, emission_capacity;
  emission *firing; size_t firing_count, firing_capacity;
  weapon_entry *weapons; size_t weapon_count;
  site_entry sites[8]; size_t site_count;
  int64_t owner_field, damage_function, radius_function, lightning_function, launch_spike_function;
  /* Restored weapon ids are copied: the decoded save tree they are read from
   * is released after restore (TS strings are values). */
  char **owned_weapons; size_t owned_weapon_count;
};
static const qa_item_id KNOWN_WEAPONS[5] = {"q1:weapon/rocketlauncher", "q1:weapon/grenadelauncher",
                                            "q1:weapon/nailgun", "q1:weapon/supernailgun",
                                            "q1:weapon/lightning"};
static qa_item_id stable_weapon(qa_q1_id1_projectile_attacks *attacks, const char *weapon) {
  for (size_t i = 0; i < sizeof KNOWN_WEAPONS / sizeof *KNOWN_WEAPONS; i++)
    if (strcmp(KNOWN_WEAPONS[i], weapon) == 0) return KNOWN_WEAPONS[i];
  for (size_t i = 0; i < attacks->owned_weapon_count; i++)
    if (strcmp(attacks->owned_weapons[i], weapon) == 0) return attacks->owned_weapons[i];
  size_t length = strlen(weapon);
  char *copy = xmalloc(length + 1);
  memcpy(copy, weapon, length + 1);
  attacks->owned_weapons = xrealloc(attacks->owned_weapons,
                                    (attacks->owned_weapon_count + 1) * sizeof *attacks->owned_weapons);
  attacks->owned_weapons[attacks->owned_weapon_count++] = copy;
  return copy;
}
static emission *find_emission(qa_q1_id1_projectile_attacks *attacks, qa_actor_id actor) {
  for (size_t i = 0; i < attacks->emission_count; i++)
    if (qa_q1qc_actor_equal(attacks->emissions[i].actor, actor)) return &attacks->emissions[i].value;
  return NULL;
}
static void set_emission(qa_q1_id1_projectile_attacks *attacks, qa_actor_id actor, emission value) {
  emission *existing = find_emission(attacks, actor);
  if (existing != NULL) { *existing = value; return; }
  if (attacks->emission_count == attacks->emission_capacity) {
    attacks->emission_capacity =
        attacks->emission_capacity == 0 ? 8 : attacks->emission_capacity * 2;
    attacks->emissions = xrealloc(attacks->emissions,
                                  attacks->emission_capacity * sizeof *attacks->emissions);
  }
  attacks->emissions[attacks->emission_count++] = (emission_entry){actor, value};
}
static const qa_item_id *find_weapon(const qa_q1_id1_projectile_attacks *attacks, int64_t fn) {
  for (size_t i = 0; i < attacks->weapon_count; i++)
    if (attacks->weapons[i].function_index == fn) return &attacks->weapons[i].weapon;
  return NULL;
}
qa_q1_id1_projectile_attacks *qa_q1_id1_projectile_attacks_create(
    qa_q1qc_source source, qa_q1qc_machine_fn machine, void *machine_ctx) {
  return qa_q1_id1_projectile_attacks_create_foreign(source, NULL, machine, machine_ctx);
}
qa_q1_id1_projectile_attacks *qa_q1_id1_projectile_attacks_create_foreign(
    qa_q1qc_source source, const qa_q1qc_foreign_source *foreign,
    qa_q1qc_machine_fn machine, void *machine_ctx) {
  qa_q1_id1_projectile_attacks *attacks = xmalloc(sizeof *attacks);
  memset(attacks, 0, sizeof *attacks);
  attacks->source = source;
  if (foreign != NULL) attacks->foreign = *foreign;
  attacks->machine = machine;
  attacks->machine_ctx = machine_ctx;
  const qa_q1qc_program *p = source.program;
  bool qw = p->digest != NULL &&
            strcmp(p->digest, "sha256:ff51cb5e77360d72b93487d89198dcf94629b92f8bae100fc6ea48a6c12a7830") == 0;
  if (strcmp(qa_q1_id1_program_binding_get(p, NULL)->attribution, "native") == 0) {
    attacks->damage_function = qa_q1qc_function_named(p, "T_Damage")->index;
    attacks->radius_function = 0;
    attacks->lightning_function = 0;
    attacks->launch_spike_function = 0;
    attacks->owner_field = -1;
    return attacks;
  }
  typedef struct weapon_name { const char *name; qa_item_id weapon; } weapon_name;
  static const weapon_name NAMES[5] = {
    {"W_FireRocket", "q1:weapon/rocketlauncher"}, {"W_FireGrenade", "q1:weapon/grenadelauncher"},
    {"W_FireSpikes", "q1:weapon/nailgun"}, {"W_FireSuperSpikes", "q1:weapon/supernailgun"},
    {"W_FireLightning", "q1:weapon/lightning"}};
  attacks->weapons = xmalloc(5 * sizeof *attacks->weapons);
  for (int i = 0; i < 5; i++)
    attacks->weapons[attacks->weapon_count++] =
        (weapon_entry){qa_q1qc_function_named(p, NAMES[i].name)->index, NAMES[i].weapon};
  attacks->damage_function = qa_q1qc_function_named(p, "T_Damage")->index;
  attacks->radius_function = qa_q1qc_function_named(p, "T_RadiusDamage")->index;
  attacks->lightning_function = qa_q1qc_function_named(p, "W_FireLightning")->index;
  attacks->launch_spike_function = qa_q1qc_function_named(p, "launch_spike")->index;
  static const site_entry QW_SITES[7] = {{3256, 147}, {580, 84}, {3900, 158}, {3973, 159},
                                         {3388, 149}, {3478, 151}, {3720, 154}};
  static const site_entry NQ_SITES[8] = {{3783, 183}, {1623, 118}, {1629, 118}, {4330, 193},
                                         {4392, 194}, {3910, 185}, {3943, 185}, {3968, 185}};
  if (qw) { memcpy(attacks->sites, QW_SITES, sizeof QW_SITES); attacks->site_count = 7; }
  else { memcpy(attacks->sites, NQ_SITES, sizeof NQ_SITES); attacks->site_count = 8; }
  const qa_q1qc_definition *owner = qa_q1qc_field_by_name(p, "owner");
  if (owner == NULL) proj_error("Missing projectile owner field");
  attacks->owner_field = owner->offset;
  return attacks;
}
void qa_q1_id1_projectile_attacks_destroy(qa_q1_id1_projectile_attacks *attacks) {
  if (attacks == NULL) return;
  free(attacks->emissions);
  free(attacks->firing);
  free(attacks->weapons);
  for (size_t i = 0; i < attacks->owned_weapon_count; i++) free(attacks->owned_weapons[i]);
  free(attacks->owned_weapons);
  free(attacks);
}
qa_q1_id1_projectile_capture *qa_q1_id1_projectile_attacks_capture(
    qa_q1_id1_projectile_attacks *attacks, size_t *out_count) {
  if (attacks->firing_count != 0) proj_error("Cannot save during projectile emission");
  qa_q1_id1_projectile_capture *entries = xmalloc(attacks->emission_count * sizeof *entries);
  for (size_t i = 0; i < attacks->emission_count; i++) {
    entries[i].actor = (qa_saved_actor_id){(double)attacks->emissions[i].actor.slot,
                                           (double)attacks->emissions[i].actor.generation};
    entries[i].owner = (qa_saved_actor_id){(double)attacks->emissions[i].value.owner.slot,
                                           (double)attacks->emissions[i].value.owner.generation};
    entries[i].emitted_at = attacks->emissions[i].value.emitted_at;
    entries[i].weapon = attacks->emissions[i].value.weapon;
  }
  *out_count = attacks->emission_count;
  return entries;
}
void qa_q1_id1_projectile_attacks_restore(qa_q1_id1_projectile_attacks *attacks,
                                          const qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "qc.projectiles"))
    qa_save_format_error("qc.projectiles", "out of memory");
  size_t count = qa_save_reader_list_count(&reader);
  typedef struct restored { qa_actor_id actor, owner; double emitted_at; qa_item_id weapon; } restored;
  restored *entries = xmalloc(count * sizeof *entries);
  /* A malformed entry raises from the readers below (TS GC frees the list). */
  qa_cleanup entries_node;
  qa_cleanup_push(&entries_node, free, entries);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, actor_reader, owner_reader, slot, generation, field_reader;
    if (!qa_save_reader_item(&reader, i, &entry)) qa_save_reader_fail(&reader, "out of memory");
    qa_saved_actor_id saved[2];
    qa_save_reader *readers[2] = {&actor_reader, &owner_reader};
    const char *names[2] = {"actor", "owner"};
    qa_actor_id referenced[2];
    /* TS object literal order: actor identity, its referenceSaved, then the owner's. */
    for (int k = 0; k < 2; k++) {
      if (!qa_save_reader_field(&entry, names[k], readers[k]))
        qa_save_reader_fail(&entry, "out of memory");
      if (!qa_save_reader_field(readers[k], "slot", &slot)) qa_save_reader_fail(&entry, "out of memory");
      saved[k].slot = qa_save_reader_integer_min(&slot, 0);
      qa_save_reader_destroy(&slot);
      if (!qa_save_reader_field(readers[k], "generation", &generation))
        qa_save_reader_fail(&entry, "out of memory");
      saved[k].generation = qa_save_reader_integer_min(&generation, 0);
      qa_save_reader_destroy(&generation);
      qa_save_reader_destroy(readers[k]);
      referenced[k] = qa_session_actor_registry_reference_saved(attacks->source.actors, saved[k], "checkpoint");
    }
    entries[i].actor = referenced[0];
    entries[i].owner = referenced[1];
    if (!qa_save_reader_field(&entry, "emittedAt", &field_reader))
      qa_save_reader_fail(&entry, "out of memory");
    entries[i].emitted_at = qa_save_reader_finite(&field_reader);
    qa_save_reader_destroy(&field_reader);
    if (!qa_save_reader_field(&entry, "weapon", &field_reader))
      qa_save_reader_fail(&entry, "out of memory");
    entries[i].weapon = qa_namespaced(&field_reader);
    qa_save_reader_destroy(&field_reader);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&reader);
  attacks->emission_count = 0;
  for (size_t i = 0; i < count; i++) {
    if (find_emission(attacks, entries[i].actor) != NULL)
      proj_error("Duplicate saved projectile emission");
    set_emission(attacks, entries[i].actor,
                 (emission){entries[i].owner, entries[i].emitted_at,
                            stable_weapon(attacks, entries[i].weapon)});
  }
  qa_cleanup_pop(&entries_node);
  free(entries);
}
static qa_q1qc_machine *proj_vm(qa_q1_id1_projectile_attacks *attacks) {
  qa_q1qc_machine *vm = attacks->machine(attacks->machine_ctx);
  if (vm->program != attacks->source.program || vm->entities != attacks->source.entities)
    proj_error("Projectile observer belongs to another VM");
  return vm;
}
static qa_actor_id proj_actor(qa_q1_id1_projectile_attacks *attacks, int64_t reference) {
  int64_t slot = qa_q1qc_entities_slot(attacks->source.entities, reference);
  qa_owned_actor actor;
  /* TS `slots.at(slot) ?? foreignActor?.(slot) ?? null`. */
  const bool found = qa_source_actor_slots_at(attacks->source.slots, slot, &actor) ||
                     (attacks->foreign.foreign_actor != NULL &&
                      attacks->foreign.foreign_actor(attacks->foreign.ctx, slot, &actor));
  if (!found || !qa_session_actor_registry_is_live(attacks->source.actors, actor.id))
    proj_error("Projectile references a free actor");
  return actor.id;
}
static void pop_firing(void *arg) {
  qa_q1_id1_projectile_attacks *attacks = arg;
  attacks->firing_count--;
}
static void composed_run(void *ctx, qa_q1qc_call_site call, qa_q1qc_function_execution execute) {
  qa_q1_id1_projectile_composed *composed = ctx;
  qa_q1_id1_projectile_attacks *attacks = composed->owner;
  const qa_item_id *weapon = find_weapon(attacks, call.function_index);
  if (weapon == NULL) {
    composed->inner.run(composed->inner.ctx, call, execute);
    return;
  }
  qa_q1qc_machine *vm = proj_vm(attacks);
  if (attacks->firing_count == attacks->firing_capacity) {
    attacks->firing_capacity = attacks->firing_capacity == 0 ? 4 : attacks->firing_capacity * 2;
    attacks->firing = xrealloc(attacks->firing, attacks->firing_capacity * sizeof *attacks->firing);
  }
  attacks->firing[attacks->firing_count++] = (emission){
      proj_actor(attacks, qa_q1qc_words_int(&vm->globals,
                                            qa_q1qc_machine_global_offset(vm, "self"))),
      qa_q1qc_words_float(&vm->globals, qa_q1qc_machine_global_offset(vm, "time")), *weapon};
  /* TS pops in a finally, which also runs when execute throws. */
  qa_cleanup node;
  qa_cleanup_push(&node, pop_firing, attacks);
  if (qa_q1qc_boundary_has(&composed->inner, call.function_index))
    composed->inner.run(composed->inner.ctx, call, execute);
  else qa_q1qc_execution_run(&execute);
  qa_cleanup_pop(&node);
  pop_firing(attacks);
}
void qa_q1_id1_projectile_attacks_compose(qa_q1_id1_projectile_composed *composed,
                                          qa_q1_id1_projectile_attacks *attacks,
                                          qa_q1qc_function_boundary inner) {
  memset(composed, 0, sizeof *composed);
  composed->owner = attacks;
  composed->inner = inner;
  composed->functions = xmalloc((inner.function_count + attacks->weapon_count) *
                                sizeof *composed->functions);
  for (size_t i = 0; i < inner.function_count; i++) {
    bool seen = false;
    for (size_t j = 0; j < composed->function_count; j++)
      if (composed->functions[j] == inner.functions[i]) seen = true;
    if (!seen) composed->functions[composed->function_count++] = inner.functions[i];
  }
  for (size_t i = 0; i < attacks->weapon_count; i++) {
    bool seen = false;
    for (size_t j = 0; j < composed->function_count; j++)
      if (composed->functions[j] == attacks->weapons[i].function_index) seen = true;
    if (!seen) composed->functions[composed->function_count++] = attacks->weapons[i].function_index;
  }
  composed->boundary = (qa_q1qc_function_boundary){composed, composed->functions,
                                                   composed->function_count, composed_run};
}
void qa_q1_id1_projectile_composed_destroy(qa_q1_id1_projectile_composed *composed) {
  free(composed->functions);
  memset(composed, 0, sizeof *composed);
}
void qa_q1_id1_projectile_attacks_observe_store(qa_q1_id1_projectile_attacks *attacks,
                                                qa_q1qc_entity_store_observation store) {
  if (store.word != attacks->owner_field) return;
  bool is_weapon = find_weapon(attacks, store.function_index) != NULL;
  if (store.function_index != attacks->launch_spike_function &&
      (!is_weapon || store.function_index == attacks->lightning_function)) return;
  if (attacks->firing_count == 0) return;
  emission current = attacks->firing[attacks->firing_count - 1];
  uint32_t bits = (uint32_t)store.after[0] | ((uint32_t)store.after[1] << 8) |
                  ((uint32_t)store.after[2] << 16) | ((uint32_t)store.after[3] << 24);
  int32_t owner_reference;
  memcpy(&owner_reference, &bits, sizeof owner_reference);
  qa_actor_id owner = proj_actor(attacks, owner_reference);
  if (!qa_q1qc_actor_equal(owner, current.owner)) return;
  qa_actor_id projectile = proj_actor(attacks, store.reference);
  size_t kept = 0;
  for (size_t i = 0; i < attacks->emission_count; i++) {
    if (qa_session_actor_registry_is_live(attacks->source.actors, attacks->emissions[i].actor))
      attacks->emissions[kept++] = attacks->emissions[i];
  }
  attacks->emission_count = kept;
  if (!qa_q1qc_actor_equal(projectile, owner)) set_emission(attacks, projectile, current);
}
bool qa_q1_id1_projectile_attacks_resolve(qa_q1_id1_projectile_attacks *attacks,
                                          qa_q1_id1_damage_call call,
                                          qa_q1_id1_projectile_attack *out) {
  if (call.call.function_index != attacks->damage_function) return false;
  bool site_matches = false;
  for (size_t i = 0; i < attacks->site_count; i++)
    if (attacks->sites[i].statement == call.call.statement)
      site_matches = attacks->sites[i].caller == call.call.caller;
  if (!site_matches) return false;
  qa_q1qc_machine *vm = proj_vm(attacks);
  if (!qa_q1qc_actor_equal(proj_actor(attacks, qa_q1qc_machine_arg_int(vm, 0)), call.target) ||
      !qa_q1qc_actor_equal(proj_actor(attacks, qa_q1qc_machine_arg_int(vm, 1)), call.inflictor) ||
      !qa_q1qc_actor_equal(proj_actor(attacks, qa_q1qc_machine_arg_int(vm, 2)), call.attacker) ||
      qa_q1qc_machine_arg_float(vm, 3) != call.amount)
    proj_error("Projectile damage arguments changed");
  emission *launch = find_emission(attacks, call.inflictor);
  emission *active = attacks->firing_count == 0 ? NULL : &attacks->firing[attacks->firing_count - 1];
  if (launch == NULL && (active == NULL || !qa_q1qc_actor_equal(call.inflictor, active->owner)))
    return false;
  emission *chosen = launch != NULL ? launch : active;
  if (chosen == NULL) return false;
  const qa_item_id *lightning = find_weapon(attacks, attacks->lightning_function);
  if (launch == NULL &&
      (active == NULL || (call.call.caller != attacks->radius_function &&
                          find_weapon(attacks, call.call.caller) == NULL &&
                          (lightning == NULL || strcmp(active->weapon, *lightning) != 0))))
    return false;
  memset(out, 0, sizeof *out);
  out->weapon = chosen->weapon;
  out->time = qa_q1qc_words_float(&vm->globals, qa_q1qc_machine_global_offset(vm, "time"));
  if (launch != NULL) {
    out->has_launch = true;
    out->launch_owner = launch->owner;
    out->launch_emitted_at = launch->emitted_at;
  }
  if (launch == NULL && active != NULL && lightning != NULL &&
      strcmp(active->weapon, *lightning) == 0 && call.call.caller != attacks->radius_function &&
      call.call.caller != attacks->lightning_function) {
    out->has_trace = true;
    out->trace_point = qa_q1qc_words_vector(&vm->globals,
                                            qa_q1qc_machine_global_offset(vm, "trace_endpos"));
    out->trace_normal = qa_q1qc_words_vector(
        &vm->globals, qa_q1qc_machine_global_offset(vm, "trace_plane_normal"));
  }
  return true;
}
