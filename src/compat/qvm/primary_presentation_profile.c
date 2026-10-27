/* See include/qa/compat/qvm/primary_presentation_profile.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/primary_presentation_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/operations.h"
#include "qa/compat/qvm/render_record.h"
#include "qa/content/mounts/paths.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

static void *xmalloc(size_t size) {
  void *result = malloc(size == 0 ? 1 : size);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  return result;
}

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("QVM memory allocation failed");
  return out;
}

static qa_save_reader item_at(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_range_error("QVM memory allocation failed");
  return out;
}


static bool digest_valid(const char *digest) {
  if (strncmp(digest, "sha256:", 7) != 0) return false;
  const char *hex = digest + 7;
  if (strlen(hex) != 64) return false;
  for (int i = 0; i < 64; i++) {
    char c = hex[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

static bool lower_equal(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
    char cb = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
    if (ca != cb) return false;
    a++;
    b++;
  }
  return *a == *b;
}

/* Mirrored parseQvmCompatibilityDeclaration selection: returns the
 * selected equipmentPresentation reader (caller destroys) or false
 * when absent; *out_profile receives the selected ABI profile. */
static bool select_equipment(const qa_save_value *value, const qa_qvmy_artifact *artifact,
                             qa_save_reader *out_reader, const char **out_profile) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "qvm-compatibility.json"))
    qa_range_error("QVM memory allocation failed");
  qa_save_reader version = field(&reader, "version");
  (void)qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_save_reader modules = field(&reader, "modules");
  size_t count = qa_save_reader_list_count(&modules);
  char (*seen)[512] = xmalloc(count * sizeof *seen);
  size_t seen_count = 0;
  bool selected = false;
  *out_profile = "q3-modern";
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item_at(&modules, i);
    qa_save_reader role_reader = field(&entry, "role");
    static const char *const roles[] = {"qagame", "cgame", "ui"};
    const char *role = qa_save_reader_choice_string(&role_reader, roles, 3);
    qa_save_reader_destroy(&role_reader);
    qa_save_reader path_reader = field(&entry, "artifactPath");
    const char *raw_path = qa_save_reader_string(&path_reader);
    char *path = qa_normalize_resource_path(raw_path);
    if (path == NULL) qa_range_error("QVM memory allocation failed");
    qa_save_reader_destroy(&path_reader);
    char key[512];
    snprintf(key, sizeof key, "%s/", role);
    size_t used = strlen(key);
    for (const char *p = path; *p != '\0' && used + 1 < sizeof key; p++)
      key[used++] = *p >= 'A' && *p <= 'Z' ? (char)(*p + 32) : *p;
    key[used] = '\0';
    for (size_t j = 0; j < seen_count; j++) {
      if (strcmp(seen[j], key) == 0)
        qa_save_reader_fail(&entry, "duplicate module compatibility declaration");
    }
    memcpy(seen[seen_count++], key, sizeof key);
    qa_save_reader digest_reader = field(&entry, "artifactDigest");
    const char *digest = qa_save_reader_string(&digest_reader);
    if (!digest_valid(digest))
      qa_save_reader_fail(&entry, "expected exact sha256 artifact digest");
    qa_save_reader_destroy(&digest_reader);
    qa_save_reader profile_reader = field(&entry, "profile");
    static const char *const profiles[] = {"q3-modern", "q3-1.16n-base"};
    const char *profile = qa_save_reader_choice_string(&profile_reader, profiles, 2);
    qa_save_reader_destroy(&profile_reader);
    qa_save_reader primary = field(&entry, "primary");
    qa_save_reader equipment = field(&entry, "equipmentPresentation");
    if (!qa_save_reader_is_undefined(&primary) && strcmp(role, "qagame") != 0)
      qa_save_reader_fail(&primary, "primary player interfaces belong to qagame");
    if (!qa_save_reader_is_undefined(&equipment) && strcmp(role, "cgame") != 0)
      qa_save_reader_fail(&equipment, "equipment presentation belongs to cgame");
    bool matches = strcmp(role, "cgame") == 0 && lower_equal(path, artifact->module.artifact_path);
    if (matches) {
      qa_save_reader digest_check = field(&entry, "artifactDigest");
      const char *entry_digest = qa_save_reader_string(&digest_check);
      if (strcmp(entry_digest, artifact->module.digest) != 0)
        qa_save_reader_fail(&entry, "compatibility declaration belongs to different artifact bytes");
      qa_save_reader_destroy(&digest_check);
      *out_profile = profile;
      if (!qa_save_reader_is_undefined(&equipment)) {
        if (selected) qa_save_reader_destroy(out_reader);
        *out_reader = equipment;
        selected = true;
        equipment.value = NULL;
        equipment.path = NULL;
      } else if (selected) {
        qa_save_reader_destroy(out_reader);
        selected = false;
      }
    }
    qa_save_reader_destroy(&primary);
    qa_save_reader_destroy(&equipment);
    qa_save_reader_destroy(&entry);
    free(path);
  }
  free(seen);
  qa_save_reader_destroy(&modules);
  qa_save_reader_destroy(&reader);
  return selected;
}

static const qa_qvmy_region_instruction *insn(const qa_qvmy_artifact *artifact, int64_t index) {
  if (index < 0 || (uint64_t)index >= artifact->instruction_count) return NULL;
  return &artifact->instructions[index];
}

static int64_t read_entry(const qa_qvmy_artifact *artifact, qa_save_reader *reader) {
  int64_t pc = (int64_t)qa_save_reader_integer_min(reader, 0);
  const qa_qvmy_region_instruction *instruction = insn(artifact, pc);
  if (instruction == NULL || instruction->opcode != QA_QVMY_OP_ENTER)
    qa_save_reader_fail(reader, "not an original function entry");
  return pc;
}

static int64_t entry_field(const qa_qvmy_artifact *artifact, const qa_save_reader *reader, const char *name) {
  qa_save_reader at = field(reader, name);
  int64_t value = read_entry(artifact, &at);
  qa_save_reader_destroy(&at);
  return value;
}

static int64_t read_decision(const qa_qvmy_artifact *artifact, qa_save_reader *reader, int64_t owner) {
  int64_t pc = (int64_t)qa_save_reader_integer_min(reader, 0);
  const qa_qvmy_region_instruction *instruction = insn(artifact, pc);
  int64_t function_entry = pc;
  while (function_entry >= 0) {
    const qa_qvmy_region_instruction *candidate = insn(artifact, function_entry);
    if (candidate != NULL && candidate->opcode == QA_QVMY_OP_ENTER) break;
    function_entry--;
  }
  if (function_entry != owner || instruction == NULL || instruction->opcode < QA_QVMY_OP_EQ ||
      instruction->opcode > QA_QVMY_OP_GEF)
    qa_save_reader_fail(reader, "visibility decision is outside its original function");
  return pc;
}

static int64_t decision_field(const qa_qvmy_artifact *artifact, const qa_save_reader *reader,
                              const char *name, int64_t owner) {
  qa_save_reader at = field(reader, name);
  int64_t value = read_decision(artifact, &at, owner);
  qa_save_reader_destroy(&at);
  return value;
}

static int64_t read_argument(qa_save_reader *reader) {
  int64_t index = (int64_t)qa_save_reader_integer_min(reader, 0);
  if (index >= 62) qa_save_reader_fail(reader, "source argument exceeds the private invocation extent");
  return index;
}

static int64_t argument_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader at = field(reader, name);
  int64_t value = read_argument(&at);
  qa_save_reader_destroy(&at);
  return value;
}

static int64_t read_word(qa_save_reader *reader, int64_t bytes) {
  int64_t offset = (int64_t)qa_save_reader_integer_min(reader, 0);
  if (offset % 4 != 0 || offset + 4 > bytes)
    qa_save_reader_fail(reader, "source word exceeds its record or is unaligned");
  return offset;
}

static int64_t word_field(const qa_save_reader *reader, const char *name, int64_t bytes) {
  qa_save_reader at = field(reader, name);
  int64_t value = read_word(&at, bytes);
  qa_save_reader_destroy(&at);
  return value;
}

static int64_t read_int32(qa_save_reader *reader) {
  double result = qa_save_reader_integer_min(reader, -2147483648.0);
  if (result > 2147483647.0) qa_save_reader_fail(reader, "expected original int32");
  return (int64_t)result;
}

static int64_t int32_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader at = field(reader, name);
  int64_t value = read_int32(&at);
  qa_save_reader_destroy(&at);
  return value;
}

bool qa_qvm_read_equipment_presentation(const qa_qvmy_artifact *artifact, qa_content_mounted *mounts,
                                        qa_q3_equipment_qvm_presentation_profile *out, bool *out_owned) {
  *out_owned = false;
  qa_content_opened_resource opened;
  bool has_declaration = qa_content_mounted_open(mounts, "qvm-compatibility.json", NULL, NULL, &opened);
  qa_save_reader equipment_reader;
  bool has_equipment = false;
  const char *declared_profile = "q3-modern";
  qa_save_value *tree = NULL;
  if (has_declaration) {
    tree = qa_parse_json_value(opened.bytes, opened.byte_length, false);
    if (tree == NULL) qa_range_error("QVM memory allocation failed");
    free(opened.bytes);
    has_equipment = select_equipment(tree, artifact, &equipment_reader, &declared_profile);
  }
  if (!has_equipment) {
    qa_q3_equipment_qvm_artifact equipment_artifact;
    memset(&equipment_artifact, 0, sizeof equipment_artifact);
    equipment_artifact.module = artifact->module;
    equipment_artifact.role = artifact->role;
    equipment_artifact.abi_profile = artifact->has_abi_profile ? artifact->abi_profile : "q3-modern";
    bool found = qa_q3_equipment_presentation_profile(&equipment_artifact, out);
    if (tree != NULL) qa_save_value_free(tree);
    return found;
  }
  const char *abi_profile = artifact->has_abi_profile ? artifact->abi_profile : "q3-modern";
  if (strcmp(artifact->role, "cgame") != 0 || strcmp(declared_profile, abi_profile) != 0)
    qa_save_reader_fail(&equipment_reader, "equipment presentation requires the declared cgame ABI");
  memset(out, 0, sizeof *out);
  *out_owned = true;
  qa_save_reader view = field(&equipment_reader, "view");
  int64_t view_entry = entry_field(artifact, &view, "entry");
  int64_t view_decision = decision_field(artifact, &view, "decision", view_entry);
  qa_save_reader taken_reader = field(&view, "taken");
  bool view_taken = qa_save_reader_boolean(&taken_reader);
  qa_save_reader_destroy(&taken_reader);
  qa_save_reader_destroy(&view);
  qa_save_reader warning = field(&equipment_reader, "warning");
  qa_save_reader states = field(&warning, "states");
  qa_save_reader held = field(&equipment_reader, "held");
  int64_t held_entry = entry_field(artifact, &held, "entry");
  const qa_qvmy_region_instruction *held_instruction = insn(artifact, held_entry);
  if (held_instruction == NULL || held_instruction->opcode != QA_QVMY_OP_ENTER)
    qa_save_reader_fail(&held, "held weapon entry disappeared");
  int64_t frame = held_instruction->operand;
  int64_t gun = word_field(&held, "gun", frame);
  if (gun < 8 || gun + QA_QVM_REF_ENTITY_BYTES > frame)
    qa_save_reader_fail(&held, "held weapon refEntity exceeds the original local frame");
  qa_save_reader status = field(&equipment_reader, "status");
  qa_save_reader kind_reader = field(&status, "kind");
  static const char *const status_kinds[] = {"regions", "functions"};
  const char *status_kind = qa_save_reader_choice_string(&kind_reader, status_kinds, 2);
  qa_save_reader_destroy(&kind_reader);
  out->hud = (double)entry_field(artifact, &equipment_reader, "hud");
  out->view_entry = (double)view_entry;
  out->view_decision = (double)view_decision;
  out->view_taken = view_taken;
  out->warning_entry = (double)entry_field(artifact, &warning, "entry");
  out->warning_state = (double)word_field(&warning, "state",
      artifact->data_length + artifact->literal_length + artifact->bss_length);
  out->warning_none = (double)int32_field(&states, "none");
  out->warning_low = (double)int32_field(&states, "low");
  out->warning_empty = (double)int32_field(&states, "empty");
  qa_save_reader_destroy(&states);
  qa_save_reader_destroy(&warning);
  out->held_entry = (double)held_entry;
  out->held_gun = (double)gun;
  out->held_parent_argument = (double)argument_field(&held, "parentArgument");
  out->held_state_argument = (double)argument_field(&held, "stateArgument");
  out->held_entity_argument = (double)argument_field(&held, "entityArgument");
  out->held_entity_number_offset = (double)word_field(&held, "entityNumberOffset", artifact->allocated_data_length);
  qa_save_reader_destroy(&held);
  qa_save_reader entries_reader = field(&status, "entries");
  size_t entry_count = qa_save_reader_list_count(&entries_reader);
  int64_t *all_entries = xmalloc((entry_count + 4) * sizeof *all_entries);
  size_t all_count = 0;
  all_entries[all_count++] = (int64_t)out->hud;
  all_entries[all_count++] = (int64_t)out->warning_entry;
  all_entries[all_count++] = held_entry;
  all_entries[all_count++] = view_entry;
  if (strcmp(status_kind, "functions") == 0) {
    out->status_is_regions = false;
    double *functions = xmalloc(entry_count * sizeof *functions);
    for (size_t i = 0; i < entry_count; i++) {
      qa_save_reader value = item_at(&entries_reader, i);
      functions[i] = (double)read_entry(artifact, &value);
      all_entries[all_count++] = (int64_t)functions[i];
      qa_save_reader_destroy(&value);
    }
    out->status_functions = functions;
    out->status_function_count = entry_count;
  } else {
    out->status_is_regions = true;
    qa_q3_equipment_hud_region *regions = xmalloc(entry_count * sizeof *regions);
    for (size_t i = 0; i < entry_count; i++) {
      qa_save_reader value = item_at(&entries_reader, i);
      int64_t owner = entry_field(artifact, &value, "entry");
      regions[i].entry = (double)owner;
      regions[i].decision = (double)decision_field(artifact, &value, "decision", owner);
      qa_save_reader region_taken = field(&value, "taken");
      regions[i].taken = qa_save_reader_boolean(&region_taken);
      qa_save_reader_destroy(&region_taken);
      qa_save_reader ammo_reader = field(&value, "ammo");
      size_t ammo_count = qa_save_reader_list_count(&ammo_reader);
      qa_q3_equipment_hud_ammo *ammo = xmalloc(ammo_count * sizeof *ammo);
      for (size_t a = 0; a < ammo_count; a++) {
        qa_save_reader region_reader = item_at(&ammo_reader, a);
        qa_save_reader start_reader = field(&region_reader, "entry");
        int64_t region_start = (int64_t)qa_save_reader_integer_min(&start_reader, 0);
        qa_save_reader_destroy(&start_reader);
        qa_save_reader join_reader = field(&region_reader, "join");
        int64_t region_join = (int64_t)qa_save_reader_integer_min(&join_reader, 0);
        qa_save_reader_destroy(&join_reader);
        qa_qvm_qualify_region(artifact->instructions, artifact->instruction_count, owner, region_start, region_join);
        ammo[a].entry = (double)region_start;
        ammo[a].join = (double)region_join;
        qa_save_reader_destroy(&region_reader);
      }
      qa_save_reader_destroy(&ammo_reader);
      regions[i].ammo = ammo;
      regions[i].ammo_count = ammo_count;
      all_entries[all_count++] = owner;
      qa_save_reader_destroy(&value);
    }
    out->status_regions = regions;
    out->status_region_count = entry_count;
  }
  qa_save_reader_destroy(&entries_reader);
  qa_save_reader_destroy(&status);
  bool repeated = false;
  for (size_t i = 0; i < all_count; i++)
    for (size_t j = i + 1; j < all_count; j++)
      if (all_entries[i] == all_entries[j]) repeated = true;
  free(all_entries);
  if (repeated || entry_count == 0)
    qa_save_reader_fail(&equipment_reader, "equipment interfaces overlap original function ownership or omit status");
  qa_save_reader_destroy(&equipment_reader);
  qa_save_value_free(tree);
  return true;
}

void qa_qvm_equipment_presentation_free(qa_q3_equipment_qvm_presentation_profile *profile) {
  free((void *)profile->status_functions);
  if (profile->status_regions != NULL) {
    for (size_t i = 0; i < profile->status_region_count; i++)
      free((void *)profile->status_regions[i].ammo);
    free((void *)profile->status_regions);
  }
  memset(profile, 0, sizeof *profile);
}
