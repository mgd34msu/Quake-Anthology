/* Port of src/content/mods/client-outputs.ts. GPL-2.0-or-later. */
#include "qa/content/mods/client_outputs.h"

#include <stdlib.h>
#include <string.h>

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static const char *field_choice(const qa_save_reader *reader, const char *name,
                                const char *const *choices, size_t count) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static bool field_present(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  bool present = !qa_save_reader_is_undefined(&f);
  qa_save_reader_destroy(&f);
  return present;
}
static void *read_one(const qa_save_reader *reader, const char *name,
                      void (*read)(void *, const qa_save_reader *, void *),
                      void *ctx, size_t size) {
  qa_save_reader f = field(reader, name);
  void *storage = malloc(size == 0 ? 1 : size);
  if (storage == NULL) qa_save_reader_fail(&f, "out of memory");
  read(ctx, &f, storage);
  qa_save_reader_destroy(&f);
  return storage;
}

qa_mod_client_output_declaration *qa_read_client_output_declarations(
    const qa_save_reader *reader,
    void (*read_scalar)(void *ctx, const qa_save_reader *reader, void *out),
    void *scalar_ctx, size_t scalar_size,
    void (*read_vector)(void *ctx, const qa_save_reader *reader, void *out),
    void *vector_ctx, size_t vector_size, size_t *out_count) {
  static const char *const kinds[] = {"view-offset", "movement-mode", "stance",
                                      "body-shape"};
  static const char *const modes[] = {"normal", "noclip", "freeze"};
  size_t count = qa_save_reader_list_count(reader);
  qa_mod_client_output_declaration *decls = count > 0
      ? (qa_mod_client_output_declaration *)calloc(count, sizeof *decls) : NULL;
  if (count > 0 && decls == NULL) return NULL;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(reader, i);
    const char *kind = field_choice(&entry, "kind", kinds, 4);
    if (strcmp(kind, "body-shape") == 0) {
      decls[i].kind = QA_MOD_CLIENT_OUTPUT_BODY_SHAPE;
      decls[i].min_vector = read_one(&entry, "min", read_vector, vector_ctx, vector_size);
      decls[i].max_vector = read_one(&entry, "max", read_vector, vector_ctx, vector_size);
    } else if (strcmp(kind, "view-offset") == 0) {
      decls[i].kind = QA_MOD_CLIENT_OUTPUT_VIEW_OFFSET;
      if (!field_present(&entry, "height")) {
        decls[i].field = read_one(&entry, "field", read_vector, vector_ctx, vector_size);
      } else {
        if (field_present(&entry, "field"))
          qa_save_reader_fail(&entry, "view offset must declare a vector or a scalar height, not both");
        decls[i].height = read_one(&entry, "height", read_scalar, scalar_ctx, scalar_size);
      }
    } else {
      decls[i].field = read_one(&entry, "field", read_scalar, scalar_ctx, scalar_size);
      if (field_present(&entry, "mask")) {
        qa_save_reader f = field(&entry, "mask");
        decls[i].has_mask = true;
        decls[i].mask = qa_save_reader_integer_min(&f, 1);
        qa_save_reader_destroy(&f);
      }
      qa_save_reader values = field(&entry, "values");
      size_t vcount = qa_save_reader_list_count(&values);
      if (strcmp(kind, "movement-mode") == 0) {
        decls[i].kind = QA_MOD_CLIENT_OUTPUT_MOVEMENT_MODE;
        qa_mod_client_mode_value *rows = vcount > 0
            ? (qa_mod_client_mode_value *)malloc(vcount * sizeof *rows) : NULL;
        if (vcount > 0 && rows == NULL) qa_save_reader_fail(&values, "out of memory");
        for (size_t j = 0; j < vcount; j++) {
          qa_save_reader row = item(&values, j);
          qa_save_reader vf = field(&row, "value");
          rows[j].value = qa_save_reader_finite(&vf);
          qa_save_reader_destroy(&vf);
          const char *mode = field_choice(&row, "mode", modes, 3);
          rows[j].mode = strcmp(mode, "normal") == 0 ? QA_MOD_CLIENT_MOVEMENT_NORMAL
                         : strcmp(mode, "noclip") == 0 ? QA_MOD_CLIENT_MOVEMENT_NOCLIP
                                                       : QA_MOD_CLIENT_MOVEMENT_FREEZE;
          qa_save_reader_destroy(&row);
        }
        decls[i].mode_values = rows;
        decls[i].mode_value_count = vcount;
      } else {
        decls[i].kind = QA_MOD_CLIENT_OUTPUT_STANCE;
        qa_mod_client_stance_value *rows = vcount > 0
            ? (qa_mod_client_stance_value *)malloc(vcount * sizeof *rows) : NULL;
        if (vcount > 0 && rows == NULL) qa_save_reader_fail(&values, "out of memory");
        for (size_t j = 0; j < vcount; j++) {
          qa_save_reader row = item(&values, j);
          qa_save_reader vf = field(&row, "value");
          rows[j].value = qa_save_reader_finite(&vf);
          qa_save_reader_destroy(&vf);
          qa_save_reader cf = field(&row, "crouched");
          rows[j].crouched = qa_save_reader_boolean(&cf);
          qa_save_reader_destroy(&cf);
          qa_save_reader_destroy(&row);
        }
        decls[i].stance_values = rows;
        decls[i].stance_value_count = vcount;
      }
      qa_save_reader_destroy(&values);
    }
    qa_save_reader_destroy(&entry);
  }
  *out_count = count;
  return decls;
}
