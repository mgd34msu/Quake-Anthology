/* Port of src/content/q1/quakec/id1-program.ts. Pinned netquake /
 * quakeworld binding tables generated mechanically from a bun JSON
 * dump of id1ProgramBinding for the pinned digests; counts and
 * first entries cross-checked. GPL-2.0-or-later. */
#include "qa/content/q1/quakec/id1_program.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static const qa_q1_id1_statement NQ_STMTS[] = {
  {1421, QA_Q1QC_OP_LOAD_F, 1580, 163, 1590},
  {1442, QA_Q1QC_OP_STORE_PF, 213, 1600, 0},
  {1450, QA_Q1QC_OP_STORE_PF, 1607, 1601, 0},
  {1454, QA_Q1QC_OP_STORE_PF, 1610, 1608, 0},
  {1492, QA_Q1QC_OP_STORE_PV, 1655, 1643, 0},
  {1526, QA_Q1QC_OP_STORE_PF, 1677, 1675, 0},
  {1532, QA_Q1QC_OP_CALL2, 1559, 0, 0},
  {1568, QA_Q1QC_OP_CALL2, 1701, 0, 0},
};
static const int64_t NQ_AXE_DAMAGE[] = {3460};
static const qa_q1_id1_attacks NQ_ATTACKS = {163, 174, 175, 171, 172, NQ_AXE_DAMAGE, 1, {170, 3580}};
static const qa_q1_environmental_site NQ_ENV[] = {
  {239, "WaterMove", 6446, "drown", "world", NULL, NULL},
  {239, "WaterMove", 6489, "lava", "world", NULL, NULL},
  {239, "WaterMove", 6509, "slime", "world", NULL, NULL},
  {243, "PlayerPostThink", 6935, "fall", "world", NULL, NULL},
  {434, "hurt_touch", 10462, "trigger", "touch", NULL, NULL},
  {375, "door_blocked", 8690, "crush", "blocked", NULL, NULL},
  {397, "secret_blocked", 9589, "crush", "blocked", NULL, NULL},
  {448, "plat_crush", 10736, "crush", "blocked", NULL, NULL},
  {451, "train_blocked", 10877, "crush", "blocked", NULL, NULL},
};
static qa_q1_id1_program_binding NQ_BINDING = {
  .kind = "netquake", .digest = "sha256:f2619787f9aa0f057246eea1665b622b4691b5c5a800b1a46133d1fe8b771580", .attribution = "pinned", .armor_field = "items", .armor_masks = {8192, 16384, 32768},
  .damage_index = 117, .damage_first_statement = 1421, .damage_parameter_start = 1580, .damage_local_words = 10, .damage_global = 520,
  .damage_kind = "sites", .health_store = 1526, .take = 1589, .death = {1532, 1559}, .pain = {1568, 1701},
  .statements = NQ_STMTS, .statement_count = sizeof NQ_STMTS / sizeof *NQ_STMTS,
  .attacks = &NQ_ATTACKS, .environment = NQ_ENV, .environment_count = sizeof NQ_ENV / sizeof *NQ_ENV};
static const qa_q1_id1_statement QW_STMTS[] = {
  {359, QA_Q1QC_OP_LOAD_F, 855, 158, 868},
  {388, QA_Q1QC_OP_STORE_PF, 207, 884, 0},
  {396, QA_Q1QC_OP_STORE_PF, 891, 885, 0},
  {400, QA_Q1QC_OP_STORE_PF, 894, 892, 0},
  {439, QA_Q1QC_OP_STORE_PV, 939, 927, 0},
  {457, QA_Q1QC_OP_STORE_PV, 965, 953, 0},
  {517, QA_Q1QC_OP_STORE_PF, 1004, 1002, 0},
  {523, QA_Q1QC_OP_CALL2, 833, 0, 0},
  {532, QA_Q1QC_OP_CALL2, 1008, 0, 0},
};
static const int64_t QW_AXE_DAMAGE[] = {2887, 2893};
static const qa_q1_id1_attacks QW_ATTACKS = {134, 145, 146, 141, 143, QW_AXE_DAMAGE, 2, {140, 3027}};
static const qa_q1_environmental_site QW_ENV[] = {
  {383, "tdeath_touch", 9808, "trigger", "touch", "teledeath", NULL},
  {383, "tdeath_touch", 9817, "trigger", "touch", "teledeath", NULL},
  {383, "tdeath_touch", 9828, "trigger", "touch", "teledeath", NULL},
  {383, "tdeath_touch", 9836, "trigger", "touch", "teledeath", NULL},
  {186, "changelevel_touch", 5410, "trigger", "touch", "exit", NULL},
  {431, "fire_touch", 10926, "lava", "touch", "fireball", NULL},
  {435, "Laser_Touch", 11091, "trigger", "touch", "laser", NULL},
  {158, "spike_touch", 3900, "trigger", "touch", "spike", NULL},
  {159, "superspike_touch", 3973, "trigger", "touch", "spike", NULL},
  {84, "T_RadiusDamage", 580, "trigger", "radius", "barrel", NULL},
  {202, "WaterMove", 6020, "drown", "world", NULL, NULL},
  {202, "WaterMove", 6063, "lava", "world", NULL, NULL},
  {202, "WaterMove", 6083, "slime", "world", NULL, NULL},
  {206, "PlayerPostThink", 6533, "fall", "world", NULL, NULL},
  {393, "hurt_touch", 10071, "trigger", "touch", NULL, NULL},
  {335, "door_blocked", 8275, "crush", "blocked", NULL, "goalentity"},
  {357, "secret_blocked", 9183, "crush", "blocked", NULL, NULL},
  {407, "plat_crush", 10349, "crush", "blocked", NULL, NULL},
  {410, "train_blocked", 10492, "crush", "blocked", NULL, NULL},
};
static qa_q1_id1_program_binding QW_BINDING = {
  .kind = "quakeworld", .digest = "sha256:ff51cb5e77360d72b93487d89198dcf94629b92f8bae100fc6ea48a6c12a7830", .attribution = "pinned", .armor_field = "items", .armor_masks = {8192, 16384, 32768},
  .damage_index = 83, .damage_first_statement = 359, .damage_parameter_start = 855, .damage_local_words = 13, .damage_global = 542,
  .damage_kind = "sites", .health_store = 517, .take = 864, .death = {523, 833}, .pain = {532, 1008},
  .statements = QW_STMTS, .statement_count = sizeof QW_STMTS / sizeof *QW_STMTS,
  .attacks = &QW_ATTACKS, .environment = QW_ENV, .environment_count = sizeof QW_ENV / sizeof *QW_ENV};

/* TS derivedBindings WeakMap. */
typedef struct cache_entry {
  const qa_q1qc_program *program;
  qa_q1_id1_program_binding *binding;
} cache_entry;
static cache_entry CACHE[64];
static size_t CACHE_COUNT;
static qa_q1_id1_program_binding *cache_get(const qa_q1qc_program *program) {
  for (size_t i = 0; i < CACHE_COUNT; i++)
    if (CACHE[i].program == program) return CACHE[i].binding;
  return NULL;
}
static void cache_put(const qa_q1qc_program *program, qa_q1_id1_program_binding *binding) {
  for (size_t i = 0; i < CACHE_COUNT; i++) {
    if (CACHE[i].program == program) { CACHE[i].binding = binding; return; }
  }
  if (CACHE_COUNT < 64) {
    CACHE[CACHE_COUNT].program = program;
    CACHE[CACHE_COUNT].binding = binding;
    CACHE_COUNT++;
  }
}
static void ensure_pinned_calls(void) {
  if (NQ_BINDING.damage_call == NULL)
    NQ_BINDING.damage_call = qa_q1qc_standard_damage_call_layout(117, 1580);
  if (QW_BINDING.damage_call == NULL)
    QW_BINDING.damage_call = qa_q1qc_standard_damage_call_layout(QW_BINDING.damage_index, QW_BINDING.damage_parameter_start);
}

typedef struct derive_ctx { const qa_q1qc_program *program; } derive_ctx;
static noreturn void reject_reason(const qa_q1qc_program *program, const char *reason) {
  qa_q1qc_program_error(program, "Unsupported native damage semantics: %s", reason);
}
static void reject_field(const qa_q1qc_program *program, const char *name) {
  char reason[128];
  (void)snprintf(reason, sizeof reason, "field %s", name);
  reject_reason(program, reason);
}

static bool word_in_set(const int64_t *set, size_t count, int64_t word) {
  for (size_t i = 0; i < count; i++) if (set[i] == word) return true;
  return false;
}
static double armor_mask(const qa_q1qc_program *program, const char *name,
                         const int64_t *mutable_set, size_t mutable_count) {
  const qa_q1qc_definition *definition = qa_q1qc_global_by_name(program, name);
  char reason[160];
  if (definition == NULL || strcmp(definition->type, "float") != 0 ||
      word_in_set(mutable_set, mutable_count, definition->offset)) {
    (void)snprintf(reason, sizeof reason, "armor inventory constant %s", name);
    reject_reason(program, reason);
  }
  double mask = (double)qa_q1qc_initial_float32(program, definition->offset);
  int64_t bits = (int64_t)mask;
  if (mask != floor(mask) || mask < 1 || mask > 2147483648.0 || (bits & (bits - 1)) != 0) {
    (void)snprintf(reason, sizeof reason, "armor inventory bit %s", name);
    reject_reason(program, reason);
  }
  return mask;
}
static const qa_q1_id1_program_binding *derive_native_binding(
    const qa_q1qc_program *program, const qa_mod_source_call *declared_damage) {
  qa_q1qc_damage_call_layout *declared_call =
      declared_damage == NULL ? NULL : qa_q1qc_damage_call_layout_create(program, declared_damage);
  qa_q1_id1_program_binding *cached = cache_get(program);
  if (cached != NULL && (declared_damage == NULL ||
                         qa_q1qc_source_call_equal(cached->damage_call->declaration, declared_damage)))
    return cached;
  if (cached != NULL && cached->damage_call->has_declaration)
    qa_q1qc_program_error(program, "QC program already has a different qualified combat call");
  qa_q1qc_damage_call_layout *call =
      declared_call != NULL ? declared_call : qa_q1qc_damage_call_layout_create(program, NULL);
  const qa_q1qc_function *damage = qa_q1qc_function_at(program, call->function_index);
  size_t mutable_capacity = 4096, mutable_count = 0;
  int64_t *mutable_set = malloc(mutable_capacity * sizeof *mutable_set);
  if (mutable_set == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < program->statement_count; i++) {
    const qa_q1qc_statement *statement = &program->statements[i];
    qa_q1qc_opcode opcode = statement->opcode;
    int64_t target = -1;
    int64_t width = 1;
    if (opcode >= QA_Q1QC_OP_STORE_F && opcode <= QA_Q1QC_OP_STORE_FN) {
      target = statement->b;
      width = opcode == QA_Q1QC_OP_STORE_V ? 3 : 1;
    } else if ((opcode >= QA_Q1QC_OP_MUL_F && opcode <= QA_Q1QC_OP_ADDRESS) ||
               (opcode >= QA_Q1QC_OP_NOT_F && opcode <= QA_Q1QC_OP_NOT_FN) ||
               (opcode >= QA_Q1QC_OP_AND && opcode <= QA_Q1QC_OP_BIT_OR)) {
      target = statement->c;
      width = opcode == QA_Q1QC_OP_MUL_FV || opcode == QA_Q1QC_OP_MUL_VF ||
                      opcode == QA_Q1QC_OP_ADD_V || opcode == QA_Q1QC_OP_SUB_V ||
                      opcode == QA_Q1QC_OP_LOAD_V
                  ? 3 : 1;
    }
    if (target < 0) continue;
    for (int64_t word = target; word < target + width; word++) {
      if (word_in_set(mutable_set, mutable_count, word)) continue;
      if (mutable_count == mutable_capacity) {
        mutable_capacity *= 2;
        mutable_set = realloc(mutable_set, mutable_capacity * sizeof *mutable_set);
        if (mutable_set == NULL) qa_range_error("out of memory");
      }
      mutable_set[mutable_count++] = word;
    }
  }
  const qa_q1qc_definition *global = qa_q1qc_global_by_name(program, damage->name);
  if (global == NULL || strcmp(global->type, "function") != 0 ||
      word_in_set(mutable_set, mutable_count, global->offset) ||
      qa_q1qc_initial_int32(program, global->offset) != damage->index)
    reject_reason(program, "damage global references");
  static const struct { const char *name; const char *type; } FIELDS[] = {
    {"health", "float"}, {"armorvalue", "float"}, {"armortype", "float"},
    {"velocity", "vector"}, {"th_pain", "function"}, {"th_die", "function"}
  };
  for (size_t i = 0; i < sizeof FIELDS / sizeof *FIELDS; i++) {
    const qa_q1qc_definition *field = qa_q1qc_field_by_name(program, FIELDS[i].name);
    if (field == NULL || strcmp(field->type, FIELDS[i].type) != 0) reject_field(program, FIELDS[i].name);
  }
  const qa_q1qc_definition *self_global = qa_q1qc_global_by_name(program, "self");
  if (self_global == NULL || strcmp(self_global->type, "entity") != 0)
    reject_reason(program, "self global");
  const qa_q1qc_definition *inventory = NULL;
  size_t inventory_count = 0;
  for (size_t i = 0; i < program->field_count; i++) {
    const qa_q1qc_definition *field = &program->fields[i];
    if (strncmp(field->name, "items", 5) != 0 || strcmp(field->type, "float") != 0) continue;
    const char *suffix = field->name + 5;
    bool digits = true;
    for (const char *p = suffix; *p != '\0'; p++) if (*p < '0' || *p > '9') digits = false;
    if (!digits) continue;
    bool all = true;
    for (int grade = 1; grade <= 3; grade++) {
      char name[64];
      (void)snprintf(name, sizeof name, "IT%s_ARMOR%d", suffix, grade);
      if (qa_q1qc_global_by_name(program, name) == NULL) all = false;
    }
    if (!all) continue;
    inventory = field;
    inventory_count++;
  }
  if (inventory_count != 1 || inventory == NULL)
    reject_reason(program, "ambiguous or missing armor inventory constants");
  const char *armor_field = inventory->name;
  char prefix[64];
  (void)snprintf(prefix, sizeof prefix, "IT%s_ARMOR", armor_field + 5);
  double masks[3];
  for (int grade = 1; grade <= 3; grade++) {
    char name[80];
    (void)snprintf(name, sizeof name, "%s%d", prefix, grade);
    masks[grade - 1] = armor_mask(program, name, mutable_set, mutable_count);
  }
  if (masks[0] == masks[1] || masks[0] == masks[2] || masks[1] == masks[2])
    reject_reason(program, "overlapping armor inventory bits");
  const qa_q1qc_definition *pain = qa_q1qc_field_by_name(program, "th_pain");
  const qa_q1qc_definition *die = qa_q1qc_field_by_name(program, "th_die");
  qa_q1_id1_reaction *reactions = NULL;
  size_t reaction_count = 0, reaction_capacity = 0;
  for (size_t f = 0; f < program->function_count; f++) {
    const qa_q1qc_function *fn = &program->functions[f];
    if (fn->first_statement <= 0) continue;
    int64_t end = qa_q1qc_function_end(program, fn);
    for (int64_t index = fn->first_statement; index < end; index++) {
      const qa_q1qc_statement *call_statement = &program->statements[index];
      if (call_statement->opcode < QA_Q1QC_OP_CALL0 || call_statement->opcode > QA_Q1QC_OP_CALL8) continue;
      int64_t word = call_statement->a;
      for (int64_t cursor = index - 1; cursor >= fn->first_statement; cursor--) {
        const qa_q1qc_statement *statement = &program->statements[cursor];
        qa_q1qc_opcode opcode = statement->opcode;
        int64_t a = statement->a, b = statement->b, c = statement->c;
        if (opcode == QA_Q1QC_OP_LOAD_FN && c == word) {
          bool is_field = false;
          for (size_t g = 0; g < program->global_count; g++)
            if (program->globals[g].offset == b && strcmp(program->globals[g].type, "field") == 0) is_field = true;
          if (!word_in_set(mutable_set, mutable_count, b) && is_field) {
            int64_t field_offset = qa_q1qc_initial_int32(program, b);
            const char *kind = pain != NULL && field_offset == pain->offset ? "pain"
                               : die != NULL && field_offset == die->offset ? "death" : NULL;
            if (kind != NULL) {
              bool seen = false;
              for (size_t r = 0; r < reaction_count; r++)
                if (reactions[r].statement == index) { reactions[r].kind = kind; seen = true; }
              if (!seen) {
                if (reaction_count == reaction_capacity) {
                  reaction_capacity = reaction_capacity == 0 ? 16 : reaction_capacity * 2;
                  reactions = realloc(reactions, reaction_capacity * sizeof *reactions);
                  if (reactions == NULL) qa_range_error("out of memory");
                }
                reactions[reaction_count].statement = index;
                reactions[reaction_count].kind = kind;
                reaction_count++;
              }
            }
          }
          break;
        }
        if (opcode == QA_Q1QC_OP_STORE_FN && b == word) { word = a; continue; }
        if ((opcode >= QA_Q1QC_OP_CALL0 && opcode <= QA_Q1QC_OP_CALL8) ||
            opcode == QA_Q1QC_OP_IF || opcode == QA_Q1QC_OP_IF_NOT || opcode == QA_Q1QC_OP_GOTO) break;
        if (opcode >= QA_Q1QC_OP_STORE_F && opcode <= QA_Q1QC_OP_STORE_FN &&
            b <= word && word < b + (opcode == QA_Q1QC_OP_STORE_V ? 3 : 1)) break;
        if (((opcode >= QA_Q1QC_OP_MUL_F && opcode <= QA_Q1QC_OP_ADDRESS) ||
             (opcode >= QA_Q1QC_OP_NOT_F && opcode <= QA_Q1QC_OP_NOT_FN) ||
             (opcode >= QA_Q1QC_OP_AND && opcode <= QA_Q1QC_OP_BIT_OR)) &&
            c <= word && word < c + 3) break;
      }
    }
  }
  free(mutable_set);
  if (reaction_count == 0) reject_reason(program, "no typed damage reaction calls");
  qa_q1_id1_program_binding *binding = malloc(sizeof *binding);
  if (binding == NULL) qa_range_error("out of memory");
  memset(binding, 0, sizeof *binding);
  binding->kind = strcmp(program->api_kind, "q1-quakeworld") == 0 ? "quakeworld" : "netquake";
  binding->attribution = "native";
  binding->digest = program->digest;
  binding->armor_field = armor_field;
  binding->armor_masks[0] = masks[0];
  binding->armor_masks[1] = masks[1];
  binding->armor_masks[2] = masks[2];
  binding->damage_index = damage->index;
  binding->damage_first_statement = damage->first_statement;
  binding->damage_parameter_start = damage->parameter_start;
  binding->damage_local_words = damage->local_words;
  binding->damage_global = global->offset;
  binding->damage_call = call;
  binding->damage_kind = "calls";
  binding->reactions = reactions;
  binding->reaction_count = reaction_count;
  cache_put(program, binding);
  return binding;
}
const qa_q1_id1_program_binding *qa_q1_derive_native_program_binding(const qa_q1qc_program *program) {
  return derive_native_binding(program, NULL);
}
const qa_q1_id1_program_binding *qa_q1_id1_program_binding_get(
    const qa_q1qc_program *program, const qa_mod_source_call *declared_damage) {
  ensure_pinned_calls();
  qa_q1_id1_program_binding *pinned = strcmp(program->digest, NQ_BINDING.digest) == 0 ? &NQ_BINDING
                                      : strcmp(program->digest, QW_BINDING.digest) == 0 ? &QW_BINDING : NULL;
  if (pinned == NULL) return derive_native_binding(program, declared_damage);
  if (declared_damage != NULL) {
    qa_q1qc_damage_call_layout *call = qa_q1qc_damage_call_layout_create(program, declared_damage);
    if (call->function_index != pinned->damage_index ||
        !qa_q1qc_damage_roles_equal(call, pinned->damage_call))
      qa_q1qc_program_error(program, "Pinned QC damage declaration differs from its original ABI");
    qa_q1_id1_program_binding *cached = cache_get(program);
    const qa_mod_source_call *cached_declaration =
        cached != NULL && cached->damage_call->has_declaration ? cached->damage_call->declaration : NULL;
    if (cached_declaration != NULL && !qa_q1qc_source_call_equal(cached_declaration, declared_damage))
      qa_q1qc_program_error(program, "QC program already has a different qualified combat call");
    qa_q1_id1_program_binding *copy = malloc(sizeof *copy);
    if (copy == NULL) qa_range_error("out of memory");
    *copy = *pinned;
    copy->damage_call = call;
    cache_put(program, copy);
  }
  bool program_qw = strcmp(program->api_kind, "q1-quakeworld") == 0;
  bool binding_qw = strcmp(pinned->kind, "quakeworld") == 0;
  if (program_qw != binding_qw)
    qa_q1qc_program_error(program, "QuakeC source requires a verified classic id1 or native QuakeWorld artifact");
  qa_q1_id1_program_binding *cached = cache_get(program);
  return cached != NULL ? cached : pinned;
}
double qa_q1_id1_damage_multiplier(qa_q1qc_machine *machine, int64_t attacker, int64_t inflictor) {
  const qa_q1_id1_program_binding *binding = qa_q1_id1_program_binding_get(machine->program, NULL);
  if (strcmp(binding->attribution, "native") == 0)
    qa_q1qc_error("progs.dat", "Native mod damage multiplier belongs to its bytecode");
  const qa_q1qc_definition *super_field = qa_q1qc_field_by_name(machine->program, "super_damage_finished");
  if (super_field == NULL) qa_q1qc_error("progs.dat", "Missing source damage field super_damage_finished");
  qa_q1qc_words words;
  qa_q1qc_entities_from_reference(machine->entities, attacker, &words);
  double finished = qa_q1qc_words_float(&words, super_field->offset);
  double time = qa_q1qc_words_float(&machine->globals, qa_q1qc_machine_global_offset(machine, "time"));
  if (finished <= time) return 1;
  if (strcmp(binding->kind, "netquake") == 0) return 4;
  /* TS field() is lazy: classname is only required on this branch. */
  const qa_q1qc_definition *classname_field = qa_q1qc_field_by_name(machine->program, "classname");
  if (classname_field == NULL) qa_q1qc_error("progs.dat", "Missing source damage field classname");
  qa_q1qc_entities_from_reference(machine->entities, inflictor, &words);
  char classname[128];
  qa_q1qc_machine_string(machine, qa_q1qc_words_int(&words, classname_field->offset), classname, sizeof classname);
  if (strcmp(classname, "door") == 0) return 1;
  double deathmatch = qa_q1qc_words_float(&machine->globals, qa_q1qc_machine_global_offset(machine, "deathmatch"));
  return deathmatch == 4 ? 8 : 4;
}
