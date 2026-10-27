/* Port of src/content/q3/presentation/cgame-body-profile.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/cgame_body_profile.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/mounts/paths.h"
#include "qa/persistence/value.h"
void qa_q3_pres_body_profile_free(qa_qvm_body_submission *submissions, size_t count) {
  if (submissions == NULL) return;
  for (size_t i = 0; i < count; i++) free((void *)submissions[i].mesh.parts);
  free(submissions);
}
static bool copy_parts(const qa_qvm_body_part_call *parts, size_t count, qa_qvm_body_submission *out) {
  if (count == 0) return true;
  qa_qvm_body_part_call *copy = (qa_qvm_body_part_call *)malloc(count * sizeof *copy);
  if (copy == NULL) return false;
  memcpy(copy, parts, count * sizeof *copy);
  out->mesh.parts = copy;
  out->mesh.part_count = count;
  return true;
}
/* TS locals(): every entry is a locals submission; only the player entry carries the mesh. */
static qa_q3_pres_body_profile_result locals(const double *entries, size_t count, double player,
                                             qa_qvm_scene_body_mesh mesh, const qa_qvm_body_part_call *parts,
                                             size_t part_count, qa_qvm_body_submission **out, size_t *out_count) {
  qa_qvm_body_submission *rows = (qa_qvm_body_submission *)calloc(count, sizeof *rows);
  if (rows == NULL) return QA_Q3_PRES_BODY_PROFILE_OOM;
  for (size_t i = 0; i < count; i++) {
    rows[i].entry = entries[i];
    rows[i].actor_argument = 0;
    rows[i].entity_number_offset = 0;
    if (entries[i] == player) {
      rows[i].has_mesh = true;
      rows[i].mesh = mesh;
      rows[i].mesh.parts = NULL;
      rows[i].mesh.part_count = 0;
      if (!copy_parts(parts, part_count, &rows[i])) {
        qa_q3_pres_body_profile_free(rows, count);
        return QA_Q3_PRES_BODY_PROFILE_OOM;
      }
    }
  }
  *out = rows;
  *out_count = count;
  return QA_Q3_PRES_BODY_PROFILE_OK;
}
static double field_integer_min(const qa_save_reader *reader, const char *name, double minimum, bool *oom) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) {
    *oom = true;
    return 0;
  }
  double value = qa_save_reader_integer_min(&field, minimum);
  qa_save_reader_destroy(&field);
  return value;
}
static double field_integer(const qa_save_reader *reader, const char *name, bool *oom) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) {
    *oom = true;
    return 0;
  }
  double value = qa_save_reader_integer(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static const char *part_literal(const char *part) {
  /* Part strings borrow the parsed value tree; map to static literals. */
  if (strcmp(part, "body") == 0) return "body";
  if (strcmp(part, "lower") == 0) return "lower";
  if (strcmp(part, "upper") == 0) return "upper";
  return "head";
}
static qa_q3_pres_body_profile_result read_declaration(const qa_qvmy_artifact *artifact, const uint8_t *bytes,
                                                       size_t length, qa_qvm_body_submission **out, size_t *out_count) {
  qa_save_value *value = qa_parse_json_value(bytes, length, false);
  if (value == NULL) return QA_Q3_PRES_BODY_PROFILE_OOM;
  qa_q3_pres_body_profile_result result = QA_Q3_PRES_BODY_PROFILE_OOM;
  qa_qvm_body_submission *rows = NULL;
  size_t count = 0;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "cgame-presentation.json")) {
    qa_save_value_free(value);
    return QA_Q3_PRES_BODY_PROFILE_OOM;
  }
  bool oom = false;
  qa_save_reader version;
  if (!qa_save_reader_field(&reader, "version", &version)) goto done;
  (void)qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  char *normalized = NULL;
  const char *declared_path = NULL, *declared_digest = NULL;
  qa_save_reader path_field, digest_field;
  if (!qa_save_reader_field(&reader, "artifactPath", &path_field)) goto done;
  declared_path = qa_save_reader_string(&path_field);
  normalized = qa_normalize_resource_path(declared_path);
  qa_save_reader_destroy(&path_field);
  if (normalized == NULL) goto done;
  /* TS || short-circuits: the digest field is read only when the path matches. */
  bool belongs = strcmp(normalized, artifact->module.artifact_path) == 0;
  free(normalized);
  if (belongs) {
    if (!qa_save_reader_field(&reader, "artifactDigest", &digest_field)) goto done;
    declared_digest = qa_save_reader_string(&digest_field);
    belongs = strcmp(declared_digest, artifact->module.digest) == 0;
    qa_save_reader_destroy(&digest_field);
  }
  if (!belongs) qa_save_reader_fail(&reader, "presentation declaration belongs to different cgame bytes");
  qa_save_reader list;
  if (!qa_save_reader_field(&reader, "bodySubmissions", &list)) goto done;
  count = qa_save_reader_list_count(&list);
  rows = (qa_qvm_body_submission *)calloc(count == 0 ? 1 : count, sizeof *rows);
  if (rows == NULL) {
    qa_save_reader_destroy(&list);
    goto done;
  }
  for (size_t i = 0; i < count && !oom; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&list, i, &item)) {
      oom = true;
      break;
    }
    qa_qvm_body_submission *row = &rows[i];
    /* TS reads reference.kind (choice) before the entry integers. */
    qa_save_reader storage;
    bool is_argument = false;
    bool storage_valid = qa_save_reader_field(&item, "reference", &storage);
    if (storage_valid) {
      qa_save_reader kind_field;
      if (qa_save_reader_field(&storage, "kind", &kind_field)) {
        static const char *const kinds[] = {"locals", "argument"};
        const char *kind = qa_save_reader_choice_string(&kind_field, kinds, 2);
        qa_save_reader_destroy(&kind_field);
        is_argument = strcmp(kind, "argument") == 0;
      } else {
        oom = true;
      }
    } else {
      oom = true;
    }
    if (!oom) row->entry = field_integer_min(&item, "entry", 0, &oom);
    if (!oom) row->actor_argument = field_integer_min(&item, "actorArgument", 0, &oom);
    if (!oom) row->entity_number_offset = field_integer_min(&item, "entityNumberOffset", 0, &oom);
    if (!oom && is_argument) {
      row->reference_is_argument = true;
      row->reference_index = field_integer_min(&storage, "index", 0, &oom);
    }
    if (storage_valid) qa_save_reader_destroy(&storage);
    qa_save_reader condition;
    if (!oom && qa_save_reader_field(&item, "when", &condition)) {
      if (!qa_save_reader_is_undefined(&condition)) {
        row->has_when = true;
        row->when_argument = field_integer_min(&condition, "argument", 0, &oom);
        if (!oom) row->when_equals = field_integer(&condition, "equals", &oom);
      }
      qa_save_reader_destroy(&condition);
    } else if (!oom) {
      oom = true;
    }
    qa_save_reader mesh;
    if (!oom && qa_save_reader_field(&item, "mesh", &mesh)) {
      if (!qa_save_reader_is_undefined(&mesh)) {
        row->has_mesh = true;
        row->mesh.entry = field_integer_min(&mesh, "entry", 0, &oom);
        if (!oom) row->mesh.entity_argument = field_integer_min(&mesh, "entityArgument", 0, &oom);
        if (!oom) row->mesh.state_argument = field_integer_min(&mesh, "stateArgument", 0, &oom);
        if (!oom) row->mesh.shader_offset = field_integer_min(&mesh, "shaderOffset", 0, &oom);
        qa_save_reader parts;
        if (!oom && qa_save_reader_field(&mesh, "parts", &parts)) {
          if (!qa_save_reader_is_undefined(&parts)) {
            size_t part_count = qa_save_reader_list_count(&parts);
            qa_qvm_body_part_call *part_rows =
                (qa_qvm_body_part_call *)calloc(part_count == 0 ? 1 : part_count, sizeof *part_rows);
            if (part_rows == NULL) {
              oom = true;
            } else {
              row->mesh.parts = part_rows;
              row->mesh.part_count = part_count;
              for (size_t p = 0; p < part_count && !oom; p++) {
                qa_save_reader part_row;
                if (!qa_save_reader_item(&parts, p, &part_row)) {
                  oom = true;
                  break;
                }
                part_rows[p].call = field_integer_min(&part_row, "call", 0, &oom);
                qa_save_reader part_field;
                if (!oom && qa_save_reader_field(&part_row, "part", &part_field)) {
                  static const char *const choices[] = {"body", "lower", "upper", "head"};
                  part_rows[p].part = part_literal(qa_save_reader_choice_string(&part_field, choices, 4));
                  qa_save_reader_destroy(&part_field);
                } else if (!oom) {
                  oom = true;
                }
                qa_save_reader_destroy(&part_row);
              }
            }
          }
          qa_save_reader_destroy(&parts);
        } else if (!oom) {
          oom = true;
        }
      }
      qa_save_reader_destroy(&mesh);
    } else if (!oom) {
      oom = true;
    }
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&list);
  if (!oom) {
    *out = rows;
    *out_count = count;
    rows = NULL;
    result = QA_Q3_PRES_BODY_PROFILE_OK;
  }
done:
  if (rows != NULL) qa_q3_pres_body_profile_free(rows, count);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(value);
  return result;
}
qa_q3_pres_body_profile_result qa_q3_pres_read_cgame_body_profile(const qa_qvmy_artifact *artifact,
                                                                  qa_content_mounted *mounts,
                                                                  qa_qvm_body_submission **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  if (artifact->role == NULL || strcmp(artifact->role, "cgame") != 0) return QA_Q3_PRES_BODY_PROFILE_NONE;
  qa_content_opened_resource opened;
  if (qa_content_mounted_open(mounts, "cgame-presentation.json", NULL, NULL, &opened)) {
    qa_q3_pres_body_profile_result result = read_declaration(artifact, opened.bytes, opened.byte_length, out, out_count);
    free(opened.bytes); /* opened bytes are caller-freed */
    return result;
  }
  const char *digest = artifact->module.digest;
  if (strcmp(digest, "sha256:a4744482c9b93852cc71f4d7ce03b3e4337e5d89844d27d272c2c16d74df07fa") == 0) {
    static const double entries[] = {45608, 81284, 46022, 47069, 47502, 47708, 47822, 48149, 47699, 45882, 45800};
    static const qa_qvm_body_part_call parts[] = {{82085, "lower"}, {82484, "upper"}, {82781, "head"}};
    qa_qvm_scene_body_mesh mesh = {80824, 0, 1, 112, NULL, 0};
    return locals(entries, sizeof entries / sizeof entries[0], 81284, mesh, parts, 3, out, out_count);
  }
  if (strcmp(digest, "sha256:14858804fb98609ed8b3b3c3b825f0a7cb544063f7e43735c884cd5e4a51157c") == 0) {
    static const double entries[] = {36612, 62937, 36804, 38561, 39712, 39826, 39870, 39534, 40520};
    static const qa_qvm_body_part_call parts[] = {{63314, "lower"}, {63546, "upper"}, {63772, "head"}};
    qa_qvm_scene_body_mesh mesh = {61925, 0, 1, 112, NULL, 0};
    return locals(entries, sizeof entries / sizeof entries[0], 62937, mesh, parts, 3, out, out_count);
  }
  return QA_Q3_PRES_BODY_PROFILE_NONE;
}
