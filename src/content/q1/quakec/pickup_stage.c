/* Port of src/content/q1/quakec/pickup-stage.ts. The pinned
 * statement/region tables transcribed from the TS source.
 * GPL-2.0-or-later. */
#include "qa/content/q1/quakec/pickup_stage.h"

#include <stdlib.h>
#include <string.h>

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

void qa_q1qc_pickup_stage_clear(qa_q1qc_pickup_stage *stage) {
  free(stage->values);
  free(stage->counters);
  free(stage->cargo_weapons);
  free(stage->regions);
  free(stage->selection_calls);
  free(stage->selection_weapons);
  memset(stage, 0, sizeof *stage);
}
void qa_q1qc_pickup_stages_destroy(qa_q1qc_pickup_stage *stages, size_t count) {
  if (stages == NULL) return;
  for (size_t i = 0; i < count; i++) qa_q1qc_pickup_stage_clear(&stages[i]);
  free(stages);
}

typedef struct stage_builder {
  qa_q1qc_pickup_stage stage;
  size_t value_capacity, counter_capacity, cargo_capacity, region_capacity;
} stage_builder;
static void builder_value(stage_builder *builder, qa_q1qc_pickup_descriptor value) {
  if (builder->stage.value_count == builder->value_capacity) {
    builder->value_capacity = builder->value_capacity == 0 ? 4 : builder->value_capacity * 2;
    builder->stage.values = xrealloc(builder->stage.values,
                                     builder->value_capacity * sizeof *builder->stage.values);
  }
  builder->stage.values[builder->stage.value_count++] = value;
}
static void builder_region(stage_builder *builder, qa_q1qc_pickup_region region) {
  if (builder->stage.region_count == builder->region_capacity) {
    builder->region_capacity = builder->region_capacity == 0 ? 4 : builder->region_capacity * 2;
    builder->stage.regions = xrealloc(builder->stage.regions,
                                      builder->region_capacity * sizeof *builder->stage.regions);
  }
  builder->stage.regions[builder->stage.region_count++] = region;
}
static qa_q1qc_pickup_region make_region(int64_t function_index, int64_t entry, int64_t exit,
                                         qa_q1qc_pickup_operation operation) {
  qa_q1qc_pickup_region region = {{function_index, entry, exit, true, false, 0, NULL},
                                  false, operation};
  return region;
}
static qa_q1qc_pickup_operation op_kind(const char *kind) {
  qa_q1qc_pickup_operation operation = {kind, 0, 0, NULL, NULL};
  return operation;
}
static qa_q1qc_pickup_operation op_decision(int64_t word) {
  qa_q1qc_pickup_operation operation = {"decision", word, 0, NULL, NULL};
  return operation;
}
static qa_pickup_resource protection_resource(void) {
  qa_pickup_resource resource = {QA_PICKUP_RESOURCE_PROTECTION, QA_PROTECTION_REGULAR, NULL};
  return resource;
}
static qa_pickup_resource inventory_resource(qa_item_id item) {
  qa_pickup_resource resource = {QA_PICKUP_RESOURCE_INVENTORY, QA_PROTECTION_REGULAR, item};
  return resource;
}
static qa_q1qc_pickup_descriptor string_descriptor(const char *value, qa_item_id item,
                                                   qa_pickup_resource resource) {
  qa_q1qc_pickup_descriptor descriptor = {0};
  descriptor.value_is_string = true;
  descriptor.value_string = value;
  descriptor.item = item;
  descriptor.has_resource = true;
  descriptor.resource = resource;
  return descriptor;
}

typedef struct stage_ctx {
  const qa_q1qc_program *program;
  const char *message; /* "Original" | "QW" */
} stage_ctx;
static void check_statement(stage_ctx *ctx, int64_t index, qa_q1qc_opcode opcode,
                            int64_t a, int64_t b, int64_t c) {
  const qa_q1qc_statement *actual =
      index >= 0 && (size_t)index < ctx->program->statement_count
          ? &ctx->program->statements[index] : NULL;
  if (actual == NULL || actual->opcode != opcode || actual->a != a || actual->b != b || actual->c != c)
    qa_q1qc_error("progs.dat", "%s pickup statement %lld differs from its qualified artifact",
                  ctx->message, (long long)index);
}
static int64_t check_fn(stage_ctx *ctx, const char *name, int64_t index, int64_t first,
                        int64_t start, int64_t words, bool require_no_params) {
  const qa_q1qc_function *original = qa_q1qc_function_named(ctx->program, name);
  if (original->index != index || original->first_statement != first ||
      original->parameter_start != start || original->local_words != words ||
      (require_no_params && original->parameter_size_count != 0) || original->named_builtin)
    qa_q1qc_error("progs.dat", "Unsupported %s pickup caller %s",
                  strcmp(ctx->message, "QW") == 0 ? "QW" : "original", name);
  return index;
}
static qa_q1qc_pickup_region decision_region(stage_ctx *ctx, int64_t function_index,
                                             int64_t entry, int64_t exit, int64_t word) {
  check_statement(ctx, exit, QA_Q1QC_OP_IF_NOT, word, 2, 0);
  check_statement(ctx, exit + 1, QA_Q1QC_OP_RETURN, 0, 0, 0);
  return make_region(function_index, entry, exit, op_decision(word));
}
static qa_q1qc_pickup_region grant_region(int64_t function_index, int64_t entry, int64_t exit) {
  return make_region(function_index, entry, exit, op_kind("grant"));
}

static const qa_item_id AMMO_ITEMS[4] = {"q1:ammo/shells", "q1:ammo/nails",
                                         "q1:ammo/rockets", "q1:ammo/cells"};

static qa_q1qc_pickup_stage *id1_stages(const qa_q1qc_program *program, size_t *out_count) {
  stage_ctx ctx = {program, "Original"};
  int64_t armor = check_fn(&ctx, "armor_touch", 128, 1949, 1931, 3, true);
  int64_t ammo = check_fn(&ctx, "ammo_touch", 142, 2389, 2145, 2, true);
  check_statement(&ctx, 1975, QA_Q1QC_OP_LOAD_F, 29, 187, 1951);
  check_statement(&ctx, 1979, QA_Q1QC_OP_GE, 1953, 1954, 1955);
  check_statement(&ctx, 1982, QA_Q1QC_OP_ADDRESS, 29, 187, 1956);
  check_statement(&ctx, 1994, QA_Q1QC_OP_STORE_PF, 1965, 1958, 0);
  check_statement(&ctx, 1995, QA_Q1QC_OP_ADDRESS, 28, 104, 1966);
  stage_builder armor_stage = {0};
  armor_stage.stage.function_index = armor;
  armor_stage.stage.descriptor_kind = "string";
  armor_stage.stage.descriptor_field = "classname";
  builder_value(&armor_stage, string_descriptor("item_armor1", "q1:item_armor1", protection_resource()));
  builder_value(&armor_stage, string_descriptor("item_armor2", "q1:item_armor2", protection_resource()));
  builder_value(&armor_stage, string_descriptor("item_armorInv", "q1:item_armorInv", protection_resource()));
  builder_region(&armor_stage, decision_region(&ctx, armor, 1975, 1980, 1955));
  builder_region(&armor_stage, grant_region(armor, 1982, 1995));

  stage_builder ammo_stage = {0};
  ammo_stage.stage.function_index = ammo;
  ammo_stage.stage.descriptor_kind = "float";
  ammo_stage.stage.descriptor_field = "weapon";
  static const int64_t BRANCHES[4][3] = {{2405, 158, 2153}, {2417, 159, 2161},
                                         {2429, 160, 2169}, {2441, 161, 2177}};
  for (int i = 0; i < 4; i++) {
    int64_t entry = BRANCHES[i][0], field = BRANCHES[i][1], temporary = BRANCHES[i][2];
    check_statement(&ctx, entry, QA_Q1QC_OP_LOAD_F, 29, field, temporary);
    check_statement(&ctx, entry + 4, QA_Q1QC_OP_ADDRESS, 29, field, temporary + 2);
    check_statement(&ctx, entry + 8, QA_Q1QC_OP_STORE_PF, temporary + 5, temporary + 2, 0);
    builder_region(&ammo_stage, decision_region(&ctx, ammo, entry, entry + 2, temporary + 1));
    builder_region(&ammo_stage, grant_region(ammo, entry + 4, entry + 9));
    qa_q1qc_pickup_descriptor descriptor = {0};
    descriptor.value_number = i + 1;
    descriptor.item = AMMO_ITEMS[i];
    descriptor.has_resource = true;
    descriptor.resource = inventory_resource(AMMO_ITEMS[i]);
    descriptor.has_supply = true;
    descriptor.supply = (qa_q1qc_pickup_supply){AMMO_ITEMS[i], false, "aflag", 0, false, 0};
    builder_value(&ammo_stage, descriptor);
  }
  check_statement(&ctx, 2450, QA_Q1QC_OP_CALL0, 1990, 0, 0);
  check_statement(&ctx, 2451, QA_Q1QC_OP_STORE_V, 29, 4, 0);
  builder_region(&ammo_stage, grant_region(ammo, 2450, 2451));
  check_statement(&ctx, 2470, QA_Q1QC_OP_LOAD_F, 29, 154, 2185);
  check_statement(&ctx, 2483, QA_Q1QC_OP_STORE_ENT, 2145, 28, 0);
  check_statement(&ctx, 2484, QA_Q1QC_OP_ADDRESS, 28, 130, 2188);
  builder_region(&ammo_stage, make_region(ammo, 2470, 2484, op_kind("weapon-selection")));

  int64_t weapon = check_fn(&ctx, "weapon_touch", 135, 2119, 2021, 6, true);
  stage_builder weapon_stage = {0};
  weapon_stage.stage.function_index = weapon;
  weapon_stage.stage.descriptor_kind = "string";
  weapon_stage.stage.descriptor_field = "classname";
  typedef struct weapon_row { const char *value; qa_item_id item, counter; int64_t entry, temporary, field, amount; } weapon_row;
  static const weapon_row WEAPONS[6] = {
    {"weapon_nailgun", "q1:weapon/nailgun", "q1:ammo/nails", 2138, 2035, 159, 304},
    {"weapon_supernailgun", "q1:weapon/supernailgun", "q1:ammo/nails", 2154, 2045, 159, 304},
    {"weapon_supershotgun", "q1:weapon/supershotgun", "q1:ammo/shells", 2170, 2055, 158, 230},
    {"weapon_rocketlauncher", "q1:weapon/rocketlauncher", "q1:ammo/rockets", 2186, 2065, 160, 230},
    {"weapon_grenadelauncher", "q1:weapon/grenadelauncher", "q1:ammo/rockets", 2202, 2075, 160, 230},
    {"weapon_lightning", "q1:weapon/lightning", "q1:ammo/cells", 2218, 2085, 161, 1861},
  };
  for (int i = 0; i < 6; i++) {
    const weapon_row *row = &WEAPONS[i];
    check_statement(&ctx, row->entry, QA_Q1QC_OP_LOAD_F, 29, 162, row->temporary);
    check_statement(&ctx, row->entry + 2, QA_Q1QC_OP_AND, 2026, row->temporary + 1, row->temporary + 2);
    check_statement(&ctx, row->entry + 8, QA_Q1QC_OP_ADDRESS, 29, row->field, row->temporary + 4);
    check_statement(&ctx, row->entry + 10, QA_Q1QC_OP_ADD_F, row->temporary + 5, row->amount, row->temporary + 6);
    check_statement(&ctx, row->entry + 11, QA_Q1QC_OP_STORE_PF, row->temporary + 6, row->temporary + 4, 0);
    builder_region(&weapon_stage, decision_region(&ctx, weapon, row->entry, row->entry + 3, row->temporary + 2));
    builder_region(&weapon_stage, grant_region(weapon, row->entry + 8, row->entry + 12));
    qa_q1qc_pickup_descriptor descriptor = string_descriptor(row->value, row->item, inventory_resource(row->item));
    descriptor.has_supply = true;
    descriptor.supply = (qa_q1qc_pickup_supply){row->counter, true, NULL, row->amount, true, 2026};
    builder_value(&weapon_stage, descriptor);
  }
  check_statement(&ctx, 2252, QA_Q1QC_OP_CALL0, 1990, 0, 0);
  check_statement(&ctx, 2270, QA_Q1QC_OP_STORE_ENT, 2025, 28, 0);
  check_statement(&ctx, 2271, QA_Q1QC_OP_IF_NOT, 2026, 2, 0);
  builder_region(&weapon_stage, grant_region(weapon, 2252, 2271));

  int64_t backpack = check_fn(&ctx, "BackpackTouch", 159, 3127, 2464, 6, true);
  check_statement(&ctx, 3135, QA_Q1QC_OP_STORE_F, 213, 2469, 0);
  check_statement(&ctx, 3136, QA_Q1QC_OP_STORE_V, 29, 4, 0);
  check_statement(&ctx, 3141, QA_Q1QC_OP_LOAD_F, 29, 162, 2476);
  check_statement(&ctx, 3145, QA_Q1QC_OP_IF_NOT, 2479, 9, 0);
  check_statement(&ctx, 3159, QA_Q1QC_OP_ADDRESS, 29, 158, 2482);
  check_statement(&ctx, 3191, QA_Q1QC_OP_CALL0, 1990, 0, 0);
  check_statement(&ctx, 3192, QA_Q1QC_OP_LOAD_F, 28, 158, 2505);
  check_statement(&ctx, 3272, QA_Q1QC_OP_STORE_V, 28, 4, 0);
  check_statement(&ctx, 3273, QA_Q1QC_OP_CALL1, 460, 0, 0);
  check_statement(&ctx, 3274, QA_Q1QC_OP_STORE_ENT, 29, 28, 0);
  check_statement(&ctx, 3275, QA_Q1QC_OP_NOT_F, 35, 0, 2518);
  check_statement(&ctx, 3283, QA_Q1QC_OP_CALL0, 1785, 0, 0);
  check_statement(&ctx, 3284, QA_Q1QC_OP_DONE, 0, 0, 0);
  stage_builder backpack_stage = {0};
  backpack_stage.stage.function_index = backpack;
  backpack_stage.stage.descriptor_kind = "cargo";
  qa_q1qc_pickup_descriptor cargo = {0};
  cargo.value_is_string = true;
  cargo.value_string = "backpack";
  cargo.item = "q1:item_backpack";
  builder_value(&backpack_stage, cargo);
  static const qa_q1qc_pickup_counter COUNTERS[4] = {
    {"ammo_shells", "q1:ammo/shells"}, {"ammo_nails", "q1:ammo/nails"},
    {"ammo_rockets", "q1:ammo/rockets"}, {"ammo_cells", "q1:ammo/cells"}};
  backpack_stage.stage.counters = xmalloc(sizeof COUNTERS);
  memcpy(backpack_stage.stage.counters, COUNTERS, sizeof COUNTERS);
  backpack_stage.stage.counter_count = 4;
  static const qa_item_id BACKPACK_WEAPONS[8] = {
    "q1:weapon/axe", "q1:weapon/shotgun", "q1:weapon/supershotgun", "q1:weapon/nailgun",
    "q1:weapon/supernailgun", "q1:weapon/grenadelauncher", "q1:weapon/rocketlauncher",
    "q1:weapon/lightning"};
  backpack_stage.stage.cargo_weapons = xmalloc(8 * sizeof *backpack_stage.stage.cargo_weapons);
  for (int i = 0; i < 8; i++)
    backpack_stage.stage.cargo_weapons[i] = (qa_q1qc_pickup_word_item){253 + i, BACKPACK_WEAPONS[i]};
  backpack_stage.stage.cargo_weapon_count = 8;
  builder_region(&backpack_stage, make_region(backpack, 3135, 3136, op_kind("admission")));
  qa_q1qc_pickup_operation ownership = {"cargo-ownership", 2479, 0, NULL, NULL};
  builder_region(&backpack_stage, make_region(backpack, 3141, 3145, ownership));
  builder_region(&backpack_stage, grant_region(backpack, 3159, 3192));
  builder_region(&backpack_stage, make_region(backpack, 3272, 3275, op_kind("consume")));
  builder_region(&backpack_stage, make_region(backpack, 3275, 3284, op_kind("consumed-selection")));

  qa_q1qc_pickup_stage *stages = xmalloc(4 * sizeof *stages);
  stages[0] = armor_stage.stage;
  stages[1] = ammo_stage.stage;
  stages[2] = weapon_stage.stage;
  stages[3] = backpack_stage.stage;
  *out_count = 4;
  return stages;
}

static qa_q1qc_pickup_region qw_region(int64_t function_index, int64_t entry, int64_t exit,
                                       qa_q1qc_pickup_operation operation) {
  qa_q1qc_pickup_region region = make_region(function_index, entry, exit, operation);
  if (strcmp(operation.kind, "counter") == 0) {
    region.region.has_standalone = true;
    region.region.standalone_saved = 1932;
  }
  return region;
}
static qa_q1qc_pickup_stage *qw_stages(const qa_q1qc_program *program, size_t *out_count) {
  stage_ctx ctx = {program, "QW"};
  int64_t armor = check_fn(&ctx, "armor_touch", 98, 1115, 1350, 3, false);
  int64_t ammo = check_fn(&ctx, "ammo_touch", 113, 1627, 1592, 2, false);
  int64_t weapon = check_fn(&ctx, "weapon_touch", 106, 1312, 1451, 7, false);
  int64_t backpack = check_fn(&ctx, "BackpackTouch", 130, 2412, 1929, 7, false);
  int64_t selection = check_fn(&ctx, "Deathmatch_Weapon", 105, 1300, 1442, 4, false);
  (void)check_fn(&ctx, "WeaponCode", 104, 1280, 1434, 0, false);
  check_statement(&ctx, 1147, QA_Q1QC_OP_LOAD_F, 29, 181, 1373);
  check_statement(&ctx, 1151, QA_Q1QC_OP_GE, 1375, 1376, 1377);
  check_statement(&ctx, 1154, QA_Q1QC_OP_ADDRESS, 29, 181, 1378);
  check_statement(&ctx, 1166, QA_Q1QC_OP_STORE_PF, 1387, 1380, 0);
  check_statement(&ctx, 1167, QA_Q1QC_OP_ADDRESS, 28, 103, 1388);
  stage_builder armor_stage = {0};
  armor_stage.stage.function_index = armor;
  armor_stage.stage.descriptor_kind = "string";
  armor_stage.stage.descriptor_field = "classname";
  builder_value(&armor_stage, string_descriptor("item_armor1", "q1:item_armor1", protection_resource()));
  builder_value(&armor_stage, string_descriptor("item_armor2", "q1:item_armor2", protection_resource()));
  builder_value(&armor_stage, string_descriptor("item_armorInv", "q1:item_armorInv", protection_resource()));
  builder_region(&armor_stage, decision_region(&ctx, armor, 1147, 1152, 1377));
  builder_region(&armor_stage, grant_region(armor, 1154, 1167));

  stage_builder ammo_stage = {0};
  ammo_stage.stage.function_index = ammo;
  ammo_stage.stage.descriptor_kind = "float";
  ammo_stage.stage.descriptor_field = "weapon";
  static const int64_t BRANCHES[4][3] = {{1643, 153, 1600}, {1655, 154, 1608},
                                         {1667, 155, 1616}, {1679, 156, 1624}};
  for (int i = 0; i < 4; i++) {
    int64_t entry = BRANCHES[i][0], field = BRANCHES[i][1], temporary = BRANCHES[i][2];
    check_statement(&ctx, entry, QA_Q1QC_OP_LOAD_F, 29, field, temporary);
    check_statement(&ctx, entry + 4, QA_Q1QC_OP_ADDRESS, 29, field, temporary + 2);
    check_statement(&ctx, entry + 8, QA_Q1QC_OP_STORE_PF, temporary + 5, temporary + 2, 0);
    builder_region(&ammo_stage, decision_region(&ctx, ammo, entry, entry + 2, temporary + 1));
    builder_region(&ammo_stage, grant_region(ammo, entry + 4, entry + 9));
    qa_q1qc_pickup_descriptor descriptor = {0};
    descriptor.value_number = i + 1;
    descriptor.item = AMMO_ITEMS[i];
    descriptor.has_resource = true;
    descriptor.resource = inventory_resource(AMMO_ITEMS[i]);
    descriptor.has_supply = true;
    descriptor.supply = (qa_q1qc_pickup_supply){AMMO_ITEMS[i], false, "aflag", 0, false, 0};
    builder_value(&ammo_stage, descriptor);
  }
  check_statement(&ctx, 1688, QA_Q1QC_OP_CALL0, 1412, 0, 0);
  check_statement(&ctx, 1711, QA_Q1QC_OP_LOAD_F, 29, 149, 1632);
  check_statement(&ctx, 1724, QA_Q1QC_OP_STORE_ENT, 1592, 28, 0);
  check_statement(&ctx, 1725, QA_Q1QC_OP_ADDRESS, 28, 125, 1635);
  builder_region(&ammo_stage, grant_region(ammo, 1688, 1689));
  builder_region(&ammo_stage, qw_region(ammo, 1711, 1725, op_kind("weapon-selection")));

  stage_builder weapon_stage = {0};
  weapon_stage.stage.function_index = weapon;
  weapon_stage.stage.descriptor_kind = "string";
  weapon_stage.stage.descriptor_field = "classname";
  typedef struct qw_weapon_row { const char *value; qa_item_id item, counter; int64_t entry, temporary, field, amount; } qw_weapon_row;
  static const qw_weapon_row WEAPONS[6] = {
    {"weapon_nailgun", "q1:weapon/nailgun", "q1:ammo/nails", 1349, 1471, 154, 298},
    {"weapon_supernailgun", "q1:weapon/supernailgun", "q1:ammo/nails", 1365, 1481, 154, 298},
    {"weapon_supershotgun", "q1:weapon/supershotgun", "q1:ammo/shells", 1381, 1491, 153, 224},
    {"weapon_rocketlauncher", "q1:weapon/rocketlauncher", "q1:ammo/rockets", 1397, 1501, 155, 224},
    {"weapon_grenadelauncher", "q1:weapon/grenadelauncher", "q1:ammo/rockets", 1413, 1511, 155, 224},
    {"weapon_lightning", "q1:weapon/lightning", "q1:ammo/cells", 1429, 1521, 156, 1276},
  };
  for (int i = 0; i < 6; i++) {
    const qw_weapon_row *row = &WEAPONS[i];
    check_statement(&ctx, row->entry, QA_Q1QC_OP_LOAD_F, 29, 157, row->temporary);
    check_statement(&ctx, row->entry + 2, QA_Q1QC_OP_AND, 1456, row->temporary + 1, row->temporary + 2);
    check_statement(&ctx, row->entry + 8, QA_Q1QC_OP_ADDRESS, 29, row->field, row->temporary + 4);
    check_statement(&ctx, row->entry + 10, QA_Q1QC_OP_ADD_F, row->temporary + 5, row->amount, row->temporary + 6);
    check_statement(&ctx, row->entry + 11, QA_Q1QC_OP_STORE_PF, row->temporary + 6, row->temporary + 4, 0);
    builder_region(&weapon_stage, decision_region(&ctx, weapon, row->entry, row->entry + 3, row->temporary + 2));
    builder_region(&weapon_stage, grant_region(weapon, row->entry + 8, row->entry + 12));
    qa_q1qc_pickup_descriptor descriptor = string_descriptor(row->value, row->item, inventory_resource(row->item));
    descriptor.has_supply = true;
    descriptor.supply = (qa_q1qc_pickup_supply){row->counter, true, NULL, row->amount, true, 1456};
    builder_value(&weapon_stage, descriptor);
  }
  check_statement(&ctx, 1466, QA_Q1QC_OP_CALL0, 1412, 0, 0);
  check_statement(&ctx, 1473, QA_Q1QC_OP_STORE_ENT, 28, 1455, 0);
  check_statement(&ctx, 1491, QA_Q1QC_OP_CALL0, 1084, 0, 0);
  check_statement(&ctx, 1492, QA_Q1QC_OP_STORE_ENT, 1455, 28, 0);
  builder_region(&weapon_stage, grant_region(weapon, 1466, 1473));
  qa_q1qc_pickup_region self_region = qw_region(weapon, 1491, 1492, op_kind("weapon-selection"));
  self_region.self_recipient = true;
  builder_region(&weapon_stage, self_region);
  static const qa_q1qc_pickup_word_item SELECTION_WEAPONS[8] = {
    {259, "q1:weapon/axe"}, {248, "q1:weapon/shotgun"}, {249, "q1:weapon/supershotgun"},
    {250, "q1:weapon/nailgun"}, {251, "q1:weapon/supernailgun"},
    {252, "q1:weapon/grenadelauncher"}, {253, "q1:weapon/rocketlauncher"},
    {254, "q1:weapon/lightning"}};
  weapon_stage.stage.has_source_selection = true;
  weapon_stage.stage.selection_function = selection;
  weapon_stage.stage.selection_calls = xmalloc(2 * sizeof *weapon_stage.stage.selection_calls);
  weapon_stage.stage.selection_calls[0] = 1486;
  weapon_stage.stage.selection_calls[1] = 1490;
  weapon_stage.stage.selection_call_count = 2;
  weapon_stage.stage.selection_weapons = xmalloc(sizeof SELECTION_WEAPONS);
  memcpy(weapon_stage.stage.selection_weapons, SELECTION_WEAPONS, sizeof SELECTION_WEAPONS);
  weapon_stage.stage.selection_weapon_count = 8;

  static const int64_t SELECTION_CALLS[4] = {1486, 1490, 2704, 2708};
  for (int i = 0; i < 4; i++) check_statement(&ctx, SELECTION_CALLS[i], QA_Q1QC_OP_CALL2, 1441, 0, 0);
  check_statement(&ctx, 2478, QA_Q1QC_OP_STORE_V, 28, 4, 0);
  check_statement(&ctx, 2479, QA_Q1QC_OP_CALL1, 479, 0, 0);
  check_statement(&ctx, 2522, QA_Q1QC_OP_STORE_ENT, 29, 28, 0);
  check_statement(&ctx, 2446, QA_Q1QC_OP_EQ_F, 364, 223, 1946);
  check_statement(&ctx, 2447, QA_Q1QC_OP_IF_NOT, 1946, 77, 0);
  check_statement(&ctx, 2448, QA_Q1QC_OP_ADDRESS, 29, 147, 1947);
  check_statement(&ctx, 2449, QA_Q1QC_OP_LOAD_F, 29, 147, 1948);
  check_statement(&ctx, 2450, QA_Q1QC_OP_ADD_F, 1948, 229, 1949);
  check_statement(&ctx, 2451, QA_Q1QC_OP_STORE_PF, 1949, 1947, 0);
  check_statement(&ctx, 2504, QA_Q1QC_OP_ADDRESS, 29, 156, 1973);
  check_statement(&ctx, 2505, QA_Q1QC_OP_STORE_PF, 207, 1973, 0);
  check_statement(&ctx, 2524, QA_Q1QC_OP_LOAD_F, 28, 157, 1977);
  check_statement(&ctx, 2525, QA_Q1QC_OP_IF_NOT, 1977, 16, 0);
  check_statement(&ctx, 2526, QA_Q1QC_OP_LOAD_F, 29, 157, 1978);
  check_statement(&ctx, 2529, QA_Q1QC_OP_EQ_F, 1980, 207, 1981);
  check_statement(&ctx, 2546, QA_Q1QC_OP_ADDRESS, 29, 153, 1984);
  check_statement(&ctx, 2565, QA_Q1QC_OP_STORE_PF, 1999, 1996, 0);
  check_statement(&ctx, 2570, QA_Q1QC_OP_LOAD_F, 29, 149, 2002);
  check_statement(&ctx, 2571, QA_Q1QC_OP_STORE_F, 2002, 1932, 0);
  check_statement(&ctx, 2572, QA_Q1QC_OP_LOAD_F, 29, 157, 2003);
  check_statement(&ctx, 2579, QA_Q1QC_OP_CALL0, 1412, 0, 0);
  check_statement(&ctx, 2660, QA_Q1QC_OP_EQ_F, 364, 222, 2021);
  check_statement(&ctx, 2661, QA_Q1QC_OP_EQ_F, 364, 224, 2022);
  check_statement(&ctx, 2662, QA_Q1QC_OP_OR, 2021, 2022, 2023);
  check_statement(&ctx, 2663, QA_Q1QC_OP_STORE_V, 1932, 4, 0);
  check_statement(&ctx, 2664, QA_Q1QC_OP_CALL1, 1433, 0, 0);
  check_statement(&ctx, 2665, QA_Q1QC_OP_EQ_F, 1, 225, 2024);
  check_statement(&ctx, 2666, QA_Q1QC_OP_STORE_V, 1932, 4, 0);
  check_statement(&ctx, 2667, QA_Q1QC_OP_CALL1, 1433, 0, 0);
  check_statement(&ctx, 2668, QA_Q1QC_OP_EQ_F, 1, 226, 2025);
  check_statement(&ctx, 2669, QA_Q1QC_OP_OR, 2024, 2025, 2026);
  check_statement(&ctx, 2670, QA_Q1QC_OP_BIT_AND, 2023, 2026, 2027);
  check_statement(&ctx, 2671, QA_Q1QC_OP_LOAD_F, 29, 155, 2028);
  check_statement(&ctx, 2672, QA_Q1QC_OP_LT, 2028, 224, 2029);
  check_statement(&ctx, 2673, QA_Q1QC_OP_BIT_AND, 2027, 2029, 2030);
  check_statement(&ctx, 2674, QA_Q1QC_OP_IF_NOT, 2030, 3, 0);
  check_statement(&ctx, 2675, QA_Q1QC_OP_ADDRESS, 29, 155, 2031);
  check_statement(&ctx, 2676, QA_Q1QC_OP_STORE_PF, 224, 2031, 0);
  check_statement(&ctx, 2690, QA_Q1QC_OP_STORE_V, 28, 4, 0);
  check_statement(&ctx, 2691, QA_Q1QC_OP_CALL1, 479, 0, 0);
  check_statement(&ctx, 2692, QA_Q1QC_OP_STORE_ENT, 29, 28, 0);
  check_statement(&ctx, 2709, QA_Q1QC_OP_CALL0, 1084, 0, 0);

  stage_builder backpack_stage = {0};
  backpack_stage.stage.function_index = backpack;
  backpack_stage.stage.descriptor_kind = "cargo";
  qa_q1qc_pickup_descriptor cargo = {0};
  cargo.value_is_string = true;
  cargo.value_string = "backpack";
  cargo.item = "q1:item_backpack";
  builder_value(&backpack_stage, cargo);
  static const qa_q1qc_pickup_counter COUNTERS[4] = {
    {"ammo_shells", "q1:ammo/shells"}, {"ammo_nails", "q1:ammo/nails"},
    {"ammo_rockets", "q1:ammo/rockets"}, {"ammo_cells", "q1:ammo/cells"}};
  backpack_stage.stage.counters = xmalloc(sizeof COUNTERS);
  memcpy(backpack_stage.stage.counters, COUNTERS, sizeof COUNTERS);
  backpack_stage.stage.counter_count = 4;
  backpack_stage.stage.cargo_weapons = xmalloc(sizeof SELECTION_WEAPONS);
  memcpy(backpack_stage.stage.cargo_weapons, SELECTION_WEAPONS, sizeof SELECTION_WEAPONS);
  backpack_stage.stage.cargo_weapon_count = 8;
  backpack_stage.stage.has_source_effect = true;
  backpack_stage.stage.source_effect_word = 364;
  backpack_stage.stage.source_effect_value = 4;
  backpack_stage.stage.has_source_selection = true;
  backpack_stage.stage.selection_function = selection;
  backpack_stage.stage.selection_calls = xmalloc(2 * sizeof *backpack_stage.stage.selection_calls);
  backpack_stage.stage.selection_calls[0] = 2704;
  backpack_stage.stage.selection_calls[1] = 2708;
  backpack_stage.stage.selection_call_count = 2;
  backpack_stage.stage.selection_weapons = xmalloc(sizeof SELECTION_WEAPONS);
  memcpy(backpack_stage.stage.selection_weapons, SELECTION_WEAPONS, sizeof SELECTION_WEAPONS);
  backpack_stage.stage.selection_weapon_count = 8;
  builder_region(&backpack_stage, qw_region(backpack, 2448, 2452, op_kind("source-effect")));
  builder_region(&backpack_stage, qw_region(backpack, 2478, 2523, op_kind("consume")));
  qa_q1qc_pickup_operation counter_cells = {"counter", 0, 0, "ammo_cells", "q1:ammo/cells"};
  builder_region(&backpack_stage, qw_region(backpack, 2504, 2506, counter_cells));
  builder_region(&backpack_stage, qw_region(backpack, 2524, 2525, op_kind("admission")));
  qa_q1qc_pickup_operation ownership = {"cargo-ownership", 1981, 0, NULL, NULL};
  builder_region(&backpack_stage, qw_region(backpack, 2526, 2530, ownership));
  builder_region(&backpack_stage, grant_region(backpack, 2546, 2566));
  qa_q1qc_pickup_operation current = {"cargo-current", 1932, 0, NULL, NULL};
  builder_region(&backpack_stage, qw_region(backpack, 2570, 2572, current));
  builder_region(&backpack_stage, grant_region(backpack, 2572, 2580));
  qa_q1qc_pickup_operation counter_rockets = {"counter", 0, 0, "ammo_rockets", "q1:ammo/rockets"};
  builder_region(&backpack_stage, qw_region(backpack, 2660, 2677, counter_rockets));
  builder_region(&backpack_stage, qw_region(backpack, 2690, 2693, op_kind("consume")));
  builder_region(&backpack_stage, qw_region(backpack, 2709, 2710, op_kind("consumed-selection")));

  qa_q1qc_pickup_stage *stages = xmalloc(4 * sizeof *stages);
  stages[0] = armor_stage.stage;
  stages[1] = ammo_stage.stage;
  stages[2] = weapon_stage.stage;
  stages[3] = backpack_stage.stage;
  *out_count = 4;
  return stages;
}

qa_q1qc_pickup_stage *qa_q1qc_pickup_stages(const qa_q1qc_program *program, size_t *out_count) {
  *out_count = 0;
  if (program->digest != NULL &&
      strcmp(program->digest, "sha256:ff51cb5e77360d72b93487d89198dcf94629b92f8bae100fc6ea48a6c12a7830") == 0)
    return qw_stages(program, out_count);
  if (program->digest == NULL ||
      strcmp(program->digest, "sha256:f2619787f9aa0f057246eea1665b622b4691b5c5a800b1a46133d1fe8b771580") != 0)
    return NULL;
  return id1_stages(program, out_count);
}
